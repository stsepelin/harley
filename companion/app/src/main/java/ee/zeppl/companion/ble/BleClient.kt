package ee.zeppl.companion.ble

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
    // One GATT write may be outstanding at a time, so writes are queued and
    // drained on onCharacteristicWrite. Matters for the ~20-chunk icon stream;
    // notif/media are single frames that pass straight through.
    private val writeQueue  = ArrayDeque<ByteArray>()
    private var writing     = false
    // Held between "services discovered, pairing kicked off" and "bond
    // completed" so we can finish TX subscription once the link is authenticated.
    private var pendingTxChar: BluetoothGattCharacteristic? = null
    private val mainHandler = android.os.Handler(android.os.Looper.getMainLooper())
    private var bondPoll: Runnable? = null
    // Auto-recovery of an asymmetric bond: `reachedServices` is per-attempt;
    // `authRecoveryDone` guards to one removeBond+re-pair per connect sequence.
    private var reachedServices = false
    private var authRecoveryDone = false

    private val scanCallback = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            // First match wins; stop scanning immediately so we don't
            // keep the radio hot. If the user has multiple clusters
            // they can disambiguate via deviceName later.
            adapter.bluetoothLeScanner?.stopScan(this)
            Log.i(TAG, "scan hit: ${result.device.address} (${result.device.name})")
            // The device is actively advertising (we just saw it), so a direct
            // connect is fast and appropriate here.
            connectTo(result.device, autoConnect = false)
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
                    // Stale/asymmetric bond: we connected to a device the phone
                    // thinks is bonded but the link failed before we could even
                    // resolve services (e.g. MTU rc=133 from a key mismatch).
                    // Drop the phone-side bond and re-pair once - otherwise Android
                    // churns (auth fail -> auto-unbond -> repair). Guarded to a
                    // single attempt per sequence.
                    if (dev != null && status != 0 && !reachedServices &&
                        !authRecoveryDone && dev.bondState == BluetoothDevice.BOND_BONDED
                    ) {
                        authRecoveryDone = true
                        Log.w(TAG, "stale bond suspected (status=$status); removing bond + re-pair")
                        removeBond(dev)
                        BleState.conn = BleConnState.CONNECTING
                        repairAfterUnbond(dev)
                        return
                    }
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

        override fun onCharacteristicWrite(
            g: BluetoothGatt,
            ch: BluetoothGattCharacteristic?,
            status: Int,
        ) {
            onWriteComplete()   // advance the write queue (next icon chunk / frame)
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
            pendingTxChar = txChr
            reachedServices = true  // got far enough that a later drop isn't a bad bond

            // The cluster's RX characteristic is gated with WRITE_AUTHEN, so we
            // must be bonded before writes (and TX CCCD) take. If not bonded
            // yet, kick off numeric-comparison pairing and stay in PAIRING - we
            // only advance to CONNECTED once the bond actually completes, so the
            // UI never claims "connected" mid-pairing.
            val device = g.device
            if (device.bondState == BluetoothDevice.BOND_BONDED) {
                subscribeTx(g, txChr)
                authRecoveryDone = false
                BleState.conn = BleConnState.CONNECTED
                Log.i(TAG, "ready; already bonded, RX resolved, TX subscribed")
            } else {
                Log.i(TAG, "no bond yet; createBond() to trigger SMP pairing")
                BleState.conn = BleConnState.PAIRING
                val ok = device.createBond()
                Log.i(TAG, "createBond() returned $ok")
                awaitBondThenFinish(device)
            }
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
     * Connect directly to a specific peripheral by MAC, skipping the scan.
     * Used both by the in-app picker (a device the user just saw advertising:
     * autoConnect=false, fast) and by Reconnect to a bonded cluster whose
     * visibility may be off (autoConnect=true parks the address on the
     * controller accept list, so the link forms whenever it advertises -
     * including directed advertising a scan filter never matches). Any in-flight
     * attempt is torn down first so this always makes progress.
     */
    @SuppressLint("MissingPermission")
    fun connectAddress(address: String, autoConnect: Boolean) {
        val adp = adapter ?: run {
            Log.w(TAG, "no adapter — bluetooth probably off")
            BleState.conn = BleConnState.IDLE
            return
        }
        adapter?.bluetoothLeScanner?.stopScan(scanCallback)
        gatt?.close()       // close() (not disconnect) so no re-arm callback fires
        gatt = null
        writeChar = null
        // Prefer the bonded device object: it carries the correct address type
        // and IRK, so autoConnect matches the accept list for the cluster's
        // directed advertising (visibility off). getRemoteDevice(mac) defaults to
        // a PUBLIC address type, which won't match a random-identity bond. Fall
        // back to the raw MAC for a not-yet-bonded target from the scanner.
        val device = adp.bondedDevices?.firstOrNull { it.address == address }
            ?: adp.getRemoteDevice(address)
        Log.i(TAG, "connect to $address (autoConnect=$autoConnect, bonded=${device.bondState == BluetoothDevice.BOND_BONDED})")
        connectTo(device, autoConnect)
    }

    @SuppressLint("MissingPermission")
    fun stop() {
        // User-initiated teardown: clear lastDevice FIRST so the
        // disconnect callback that follows can't re-arm a reconnect.
        lastDevice = null
        cancelBondPoll()
        pendingTxChar = null
        adapter?.bluetoothLeScanner?.stopScan(scanCallback)
        gatt?.disconnect()
        gatt?.close()
        gatt = null
        writeChar = null
        clearWrites()
        BleState.deviceName = null
        BleState.conn = BleConnState.IDLE
        // Deliberate disconnect: drop last-known telemetry so Ride shows the
        // clean offline state (a link-drop keeps it for the reconnect window).
        TelemetryState.clear()
        IconSender.reset()   // re-send icons after any reconnect
    }

    @Synchronized
    fun write(bytes: ByteArray) {
        writeQueue.addLast(bytes)
        if (!writing) pump()
    }

    // Send the head of the queue if nothing is in flight. WRITE_NO_RESPONSE
    // matches the firmware characteristic flags and keeps us off the slow
    // request/response loop; completion still fires onCharacteristicWrite, which
    // is what advances the queue (one outstanding write at a time).
    @SuppressLint("MissingPermission")
    @Synchronized
    private fun pump() {
        if (writing) return
        val g = gatt
        val c = writeChar
        if (g == null || c == null) { writeQueue.clear(); return }
        val bytes = writeQueue.firstOrNull() ?: return
        writing = true
        val rc = g.writeCharacteristic(c, bytes, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE)
        BleState.lastTx = bytes.joinToString(" ") { "%02X".format(it) }
        if (rc == android.bluetooth.BluetoothStatusCodes.SUCCESS) {
            writeQueue.removeFirst()
        } else {
            // Transient congestion (often GATT busy): keep the frame, retry soon.
            Log.w(TAG, "writeCharacteristic rc=$rc")
            writing = false
            mainHandler.postDelayed({ pump() }, 15)
        }
    }

    @Synchronized
    private fun onWriteComplete() {
        writing = false
        pump()
    }

    @Synchronized
    private fun clearWrites() {
        writeQueue.clear()
        writing = false
    }

    // --- internals -----------------------------------------------------

    @SuppressLint("MissingPermission")
    private fun connectTo(device: BluetoothDevice, autoConnect: Boolean) {
        lastDevice = device
        reachedServices = false
        BleState.deviceName = device.name ?: device.address
        BleState.conn = BleConnState.CONNECTING
        gatt = device.connectGatt(appContext, autoConnect, gattCallback)
    }

    // Reflection: BluetoothDevice.removeBond() is @hide but stable since API 1.
    @SuppressLint("MissingPermission")
    private fun removeBond(device: BluetoothDevice): Boolean = try {
        (device.javaClass.getMethod("removeBond").invoke(device) as? Boolean) ?: false
    } catch (t: Throwable) {
        Log.w(TAG, "removeBond reflection failed: $t")
        false
    }

    // After removeBond() (async), wait for the bond to actually clear, then do a
    // fresh direct connect so onServicesDiscovered re-pairs from scratch.
    @SuppressLint("MissingPermission")
    private fun repairAfterUnbond(device: BluetoothDevice) {
        val poll = object : Runnable {
            private var tries = 0
            override fun run() {
                if (device.bondState == BluetoothDevice.BOND_NONE || tries++ > 25) {
                    Log.i(TAG, "re-pairing after unbond")
                    connectTo(device, autoConnect = false)
                } else {
                    mainHandler.postDelayed(this, 200)
                }
            }
        }
        mainHandler.postDelayed(poll, 200)
    }

    // Enable the TX (cluster → phone) notify channel: tell Android to deliver
    // notifications locally, then write the CCCD on the peer so it starts
    // sending. Missing either half is a silent failure.
    @SuppressLint("MissingPermission")
    private fun subscribeTx(g: BluetoothGatt, txChr: BluetoothGattCharacteristic?) {
        if (txChr == null) {
            Log.w(TAG, "TX characteristic not found — call/media commands won't work")
            return
        }
        if (!g.setCharacteristicNotification(txChr, true)) {
            Log.w(TAG, "setCharacteristicNotification(TX) returned false")
        }
        val cccd = txChr.getDescriptor(CCCD_UUID)
        if (cccd != null) {
            val rc = g.writeDescriptor(cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
            if (rc != android.bluetooth.BluetoothStatusCodes.SUCCESS) {
                Log.w(TAG, "writeDescriptor(CCCD) rc=$rc")
            }
        } else {
            Log.w(TAG, "TX characteristic has no CCCD — cluster won't notify")
        }
    }

    // Poll the OS bond state after createBond() until it settles, then finish:
    // BONDED -> subscribe TX + CONNECTED; a decline (BONDING then back to NONE)
    // or a long timeout -> DISCONNECTED. Polling is deterministic and avoids the
    // broadcast-delivery quirks that left the state stuck at PAIRING.
    @SuppressLint("MissingPermission")
    private fun awaitBondThenFinish(device: BluetoothDevice) {
        cancelBondPoll()
        val deadline = SystemClock.uptimeMillis() + 60_000  // time to confirm the code
        var sawBonding = false
        val poll = object : Runnable {
            override fun run() {
                if (BleState.conn != BleConnState.PAIRING) { bondPoll = null; return }
                when (device.bondState) {
                    BluetoothDevice.BOND_BONDED -> {
                        Log.i(TAG, "bond confirmed; finishing TX subscription")
                        gatt?.let { subscribeTx(it, pendingTxChar) }
                        authRecoveryDone = false
                        BleState.conn = BleConnState.CONNECTED
                        bondPoll = null
                    }
                    BluetoothDevice.BOND_BONDING -> {
                        sawBonding = true
                        if (SystemClock.uptimeMillis() > deadline) giveUp() else repost()
                    }
                    else -> {  // BOND_NONE: declined if we'd started bonding, else keep waiting
                        if (sawBonding || SystemClock.uptimeMillis() > deadline) giveUp() else repost()
                    }
                }
            }
            private fun repost() = mainHandler.postDelayed(this, 400)
            private fun giveUp() {
                Log.w(TAG, "pairing not completed; disconnecting")
                gatt?.disconnect()
                BleState.conn = BleConnState.DISCONNECTED
                bondPoll = null
            }
        }
        bondPoll = poll
        mainHandler.postDelayed(poll, 400)
    }

    private fun cancelBondPoll() {
        bondPoll?.let { mainHandler.removeCallbacks(it) }
        bondPoll = null
    }

    private companion object {
        const val TAG = "BleClient"
        // Client Characteristic Configuration Descriptor — the standard
        // 0x2902 UUID. Writing ENABLE_NOTIFICATION_VALUE here tells the
        // peripheral to start pushing notifications on the parent char.
        val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }
}
