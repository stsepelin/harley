package ee.zeppl.companion.ble

import android.util.Log

/**
 * Single-writer seam for wire-format bytes destined for the cluster.
 *
 * Until the BLE central is wired we just log the hex; once the GATT
 * service exists it overwrites [send] with a write-without-response
 * call. Keeping this as a `var` (rather than a subscriber list) means
 * the notification listener doesn't have to know whether anyone is
 * listening — there's exactly one consumer at any time.
 */
object OutboundSink {

    @Volatile
    var send: (ByteArray) -> Unit = { bytes ->
        Log.d(TAG, bytes.joinToString(" ") { "%02X".format(it) })
    }

    private const val TAG = "ZepplOutbound"
}
