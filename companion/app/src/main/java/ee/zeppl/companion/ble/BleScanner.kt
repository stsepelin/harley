package ee.zeppl.companion.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.ParcelUuid
import android.util.Log

/**
 * BLE scanner decoupled from connection. Emits every advertisement that
 * passes the SERVICE_UUID filter, keyed by MAC so the UI can dedupe.
 *
 * Separation from BleClient lets the user see *which* cluster they're
 * connecting to — useful on a bench with multiple devices, and the
 * only sane recovery path when Samsung's BT settings hides paired
 * peripherals from the system scan list.
 */
class BleScanner(
    private val appContext: Context,
    private val onFound: (ScannedDevice) -> Unit,
) {
    data class ScannedDevice(
        val address: String,
        val name:    String?,
        val rssi:    Int,
    )

    private val adapter: BluetoothAdapter? =
        appContext.getSystemService(BluetoothManager::class.java).adapter

    private val callback = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val d = result.device
            onFound(ScannedDevice(
                address = d.address,
                name    = d.name ?: result.scanRecord?.deviceName,
                rssi    = result.rssi,
            ))
        }

        override fun onScanFailed(errorCode: Int) {
            Log.w(TAG, "scan failed: $errorCode")
        }
    }

    private var scanning = false

    @SuppressLint("MissingPermission")
    fun start() {
        if (scanning) return
        val scanner = adapter?.bluetoothLeScanner ?: return
        // SERVICE_UUID is in the cluster's scan response, not the adv
        // packet — the 31-byte limit doesn't leave room for a 128-bit
        // UUID alongside the name. SCAN_MODE_LOW_LATENCY is active
        // scanning, so scan-responses are fetched and the filter matches.
        val filter = ScanFilter.Builder()
            .setServiceUuid(ParcelUuid(Protocol.SERVICE_UUID))
            .build()
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .setCallbackType(ScanSettings.CALLBACK_TYPE_ALL_MATCHES)
            .build()
        scanner.startScan(listOf(filter), settings, callback)
        scanning = true
    }

    @SuppressLint("MissingPermission")
    fun stop() {
        if (!scanning) return
        adapter?.bluetoothLeScanner?.stopScan(callback)
        scanning = false
    }

    private companion object {
        const val TAG = "BleScanner"
    }
}
