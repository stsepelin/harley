package ee.zeppl.companion.ble

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

enum class BleConnState {
    IDLE, SCANNING, CONNECTING,
    /** GATT is up but SMP pairing (numeric comparison) isn't confirmed yet. */
    PAIRING,
    CONNECTED, DISCONNECTED,
    /** Link lost (power cycle / out of range); background reconnect armed. */
    WAITING,
}

/**
 * Observable connection state, shared between the foreground service
 * (which writes it) and the Compose UI (which reads it). Lives as a
 * top-level singleton so the UI doesn't have to bind to the service —
 * the moment Compose reads `BleState.conn` it subscribes to changes.
 */
object BleState {
    var conn:       BleConnState by mutableStateOf(BleConnState.IDLE)
    var deviceName: String?      by mutableStateOf(null)
    /** Last hex line written over GATT — handy diagnostic in the UI. */
    var lastTx:     String?      by mutableStateOf(null)
}
