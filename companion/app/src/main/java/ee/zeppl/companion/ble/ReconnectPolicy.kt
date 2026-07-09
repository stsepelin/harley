package ee.zeppl.companion.ble

/**
 * Decides whether the client should silently re-arm a background
 * connection after a link drop, keyed on the GATT status delivered with
 * onConnectionStateChange(STATE_DISCONNECTED).
 *
 * The cluster reboots on every ignition cycle, and riding out of BLE
 * range mid-stop is routine — both surface as a supervision timeout,
 * and both should heal without the rider touching the phone. What must
 * NOT auto-heal is a deliberate disconnect: the cluster's PHONE row
 * ("TAP TO DISCONNECT") terminates from the remote side, and the
 * companion's own Disconnect button terminates locally. Reconnecting
 * after those would make both buttons useless.
 */
object ReconnectPolicy {
    const val STATUS_SUPERVISION_TIMEOUT = 8   // 0x08 — power cycle / out of range
    const val STATUS_REMOTE_TERMINATED   = 19  // 0x13 — cluster chose to disconnect
    const val STATUS_LOCAL_TERMINATED    = 22  // 0x16 — we chose to disconnect
    const val STATUS_LMP_TIMEOUT         = 34  // 0x22 — link-layer went unresponsive
    const val STATUS_FAILED_TO_ESTABLISH = 62  // 0x3E — background attempt fizzled

    fun shouldReconnect(status: Int): Boolean = when (status) {
        STATUS_SUPERVISION_TIMEOUT,
        STATUS_LMP_TIMEOUT,
        // A fizzled background attempt means the cluster isn't back yet
        // — stay armed and keep waiting rather than giving up.
        STATUS_FAILED_TO_ESTABLISH -> true
        else -> false
    }
}
