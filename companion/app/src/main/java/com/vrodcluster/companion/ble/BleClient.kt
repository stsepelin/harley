package com.vrodcluster.companion.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.os.SystemClock
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.ParcelUuid
import android.util.Log
import java.util.UUID

/**
 * Scans for a peripheral advertising [Protocol.SERVICE_UUID], connects
 * to the first match, requests a 247-byte MTU (enough for the largest
 * NOTIF payload in one ATT write), then routes every byte handed to
 * it via [write] to the RX characteristic.
 *
 * Callbacks (scanner + GATT) all fire on the main looper of the
 * binder thread the framework hands us; the foreground service is
 * already on main, so we don't add any synchronisation here.
 *
 * Permission annotations: minSdk 36 means BLUETOOTH_SCAN / _CONNECT
 * are runtime; [BleAccess.allGranted] guards entry points before
 * this class is touched.
 */
class BleClient(
    private val appContext: Context,
    /**
     * Invoked on the binder thread (Android's default for GATT callbacks)
     * when a decoded cluster→phone command arrives. The caller is
     * responsible for switching threads if its dispatch needs the main
     * looper — TelecomManager/MediaController are safe to call from
     * the binder thread, so most consumers don't need to.
     */
    private val onCommand: (ClientCommand) -> Unit = {},
) {

    private val adapter    = appContext.getSystemService(BluetoothManager::class.java).adapter
    private var gatt:       BluetoothGatt?               = null
    private var writeChar:  BluetoothGattCharacteristic? = null
    private var lastDevice: BluetoothDevice?             = null

    private val scanCallback = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            // First match wins; stop scanning immediately so we don't
            // keep the radio hot. If the user has multiple clusters
            // they can disambiguate via deviceName later.
            adapter.bluetoothLeScanner?.stopScan(this)
            Log.i(TAG, "scan hit: ${result.device.address} (${result.device.name})")
            connectTo(result.device)
        }

        override fun onScanFailed(errorCode: Int) {
            Log.w(TAG, "scan failed: $errorCode")
            BleState.conn = BleConnState.IDLE
        }
    }

    // Explicit type: the reconnect arm inside onConnectionStateChange
    // references gattCallback from its own initializer, which Kotlin's
    // inference can't resolve without the annotation.
    private val gattCallback: BluetoothGattCallback = object : BluetoothGattCallback() {

        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    Log.i(TAG, "gatt connected; requesting MTU 247")
                    // Larger MTU = full NOTIF payload (~150B incl. UTF-8
                    // sender + 128B message) in one ATT write. The
                    // cluster supports LE-2M but won't reject a smaller
                    // negotiated MTU if the link can't sustain it.
                    g.requestMtu(247)
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    Log.i(TAG, "gatt disconnected; status=$status")
                    writeChar = null
                    g.close()
                    if (gatt === g) gatt = null
                    val dev = lastDevice
                    if (dev != null && ReconnectPolicy.shouldReconnect(status)) {
                        // The cluster reboots on every ignition cycle;
                        // autoConnect=true parks its address on the
                        // controller accept list so the link re-forms as
                        // soon as it advertises again — including Stage 8
                        // directed advertising, which never matches a
                        // scan filter.
                        Log.i(TAG, "link lost; arming background reconnect to ${dev.address}")
                        BleState.conn = BleConnState.WAITING
                        gatt = dev.connectGatt(appContext, /*autoConnect=*/true, gattCallback)
                    } else {
                        BleState.conn = BleConnState.DISCONNECTED
                    }
                }
            }
        }

        @SuppressLint("MissingPermission")
        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            Log.i(TAG, "mtu=$mtu status=$status — discovering services")
            g.discoverServices()
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            val svc = g.getService(Protocol.SERVICE_UUID)
            val rxChr = svc?.getCharacteristic(Protocol.RX_UUID)
            val txChr = svc?.getCharacteristic(Protocol.TX_UUID)
            if (rxChr == null) {
                Log.w(TAG, "RX characteristic not found on device")
                BleState.conn = BleConnState.DISCONNECTED
                return
            }
            writeChar = rxChr

            // Force SMP pairing if the link isn't authenticated yet. The
            // cluster's RX characteristic is gated with WRITE_AUTHEN; our
            // writes are WRITE_TYPE_NO_RESPONSE which would silently fail
            // against an unbonded link (no Write Response to carry the
            // insufficient-authentication error back to us), so we'd
            // appear connected but every notification gets dropped at the
            // peripheral. Explicit createBond() triggers numeric-
            // comparison pairing with the cluster's screen_pairing UI.
            val device = g.device
            if (device.bondState != BluetoothDevice.BOND_BONDED) {
                Log.i(TAG, "no bond yet; createBond() to trigger SMP pairing")
                val ok = device.createBond()
                Log.i(TAG, "createBond() returned $ok")
            }

            // Subscribe to the TX (cluster → phone) notify channel. Two
            // steps: tell Android to deliver notifications for this
            // characteristic locally, then write the CCCD on the peer
            // so the cluster will actually start sending them. Missing
            // either half is a silent failure — the other side notifies
            // into the void or our local pipe drops the bytes.
            if (txChr != null) {
                if (!g.setCharacteristicNotification(txChr, true)) {
                    Log.w(TAG, "setCharacteristicNotification(TX) returned false")
                }
                val cccd = txChr.getDescriptor(CCCD_UUID)
                if (cccd != null) {
                    val rc = g.writeDescriptor(
                        cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                    )
                    if (rc != android.bluetooth.BluetoothStatusCodes.SUCCESS) {
                        Log.w(TAG, "writeDescriptor(CCCD) rc=$rc")
                    }
                } else {
                    Log.w(TAG, "TX characteristic has no CCCD — cluster won't notify")
                }
            } else {
                Log.w(TAG, "TX characteristic not found — call/media commands won't work")
            }

            BleState.conn = BleConnState.CONNECTED
            Log.i(TAG, "ready; RX handle resolved, TX subscribed")
        }

        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            if (characteristic.uuid != Protocol.TX_UUID) return
            // Telemetry frames share the TX channel with call/media commands,
            // distinguished by the leading type byte.
            if (value.isNotEmpty() && value[0] == TelemetryCodec.TYPE) {
                val frame = TelemetryCodec.decode(value)
                if (frame == null) {
                    Log.w(TAG, "dropping unparseable telemetry, ${value.size} bytes")
                } else {
                    TelemetryState.apply(frame, SystemClock.uptimeMillis())
                }
                return
            }
            val cmd = ClientProtocol.decode(value)
            if (cmd == null) {
                Log.w(TAG, "dropping unparseable TX notify, ${value.size} bytes")
                return
            }
            onCommand(cmd)
        }
    }

    // --- public API ----------------------------------------------------

    @SuppressLint("MissingPermission")
    fun start() {
        if (BleState.conn != BleConnState.IDLE && BleState.conn != BleConnState.DISCONNECTED) return
        val scanner = adapter?.bluetoothLeScanner ?: run {
            Log.w(TAG, "no BLE scanner — bluetooth probably off")
            BleState.conn = BleConnState.IDLE
            return
        }
        val filter = ScanFilter.Builder()
            .setServiceUuid(ParcelUuid(Protocol.SERVICE_UUID))
            .build()
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        BleState.conn = BleConnState.SCANNING
        scanner.startScan(listOf(filter), settings, scanCallback)
        Log.i(TAG, "scanning for cluster")
    }

    /**
     * Connect directly to a specific BLE peripheral by MAC, skipping
     * the scan-and-pick path. Used by the in-app device picker so the
     * user has explicit control over which cluster they're connecting
     * to (and the connect path works even when the radio's adv mode
     * makes the cluster invisible to a generic scan filter).
     */
    @SuppressLint("MissingPermission")
    fun startWithAddress(address: String) {
        if (BleState.conn != BleConnState.IDLE && BleState.conn != BleConnState.DISCONNECTED) return
        val adp = adapter ?: run {
            Log.w(TAG, "no adapter — bluetooth probably off")
            BleState.conn = BleConnState.IDLE
            return
        }
        val device = adp.getRemoteDevice(address)
        Log.i(TAG, "direct connect to $address")
        connectTo(device)
    }

    @SuppressLint("MissingPermission")
    fun stop() {
        // User-initiated teardown: clear lastDevice FIRST so the
        // disconnect callback that follows can't re-arm a reconnect.
        lastDevice = null
        adapter?.bluetoothLeScanner?.stopScan(scanCallback)
        gatt?.disconnect()
        gatt?.close()
        gatt = null
        writeChar = null
        BleState.deviceName = null
        BleState.conn = BleConnState.IDLE
    }

    @SuppressLint("MissingPermission")
    fun write(bytes: ByteArray) {
        val g = gatt ?: return
        val c = writeChar ?: return
        // WRITE_NO_RESPONSE matches the firmware characteristic flags
        // (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP) and
        // keeps us off the slow request/response loop for things like
        // PlaybackState position updates.
        val rc = g.writeCharacteristic(
            c, bytes, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
        )
        BleState.lastTx = bytes.joinToString(" ") { "%02X".format(it) }
        if (rc != android.bluetooth.BluetoothStatusCodes.SUCCESS) {
            Log.w(TAG, "writeCharacteristic rc=$rc")
        }
    }

    // --- internals -----------------------------------------------------

    @SuppressLint("MissingPermission")
    private fun connectTo(device: BluetoothDevice) {
        lastDevice = device
        BleState.deviceName = device.name ?: device.address
        BleState.conn = BleConnState.CONNECTING
        gatt = device.connectGatt(appContext, /*autoConnect=*/false, gattCallback)
    }

    private companion object {
        const val TAG = "BleClient"
        // Client Characteristic Configuration Descriptor — the standard
        // 0x2902 UUID. Writing ENABLE_NOTIFICATION_VALUE here tells the
        // peripheral to start pushing notifications on the parent char.
        val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }
}
