package ee.zeppl.companion.ble

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.telephony.TelephonyCallback
import android.telephony.TelephonyManager
import android.util.Log
import androidx.core.content.ContextCompat

/**
 * Mirrors the phone's own call state to the cluster so the call banner follows
 * what the rider does ON THE PHONE (answer / hang up), not just the cluster
 * buttons. OFFHOOK -> the call is active; IDLE -> it ended. RINGING is left to
 * the dialer's incoming notification, which already drives the banner.
 * Registered/unregistered by [BleService] over the link's lifetime.
 */
class CallStateTracker(context: Context) {

    private val app = context.applicationContext
    private val tm = app.getSystemService(TelephonyManager::class.java)

    private val callback = object : TelephonyCallback(), TelephonyCallback.CallStateListener {
        override fun onCallStateChanged(state: Int) {
            when (state) {
                TelephonyManager.CALL_STATE_OFFHOOK -> OutboundSink.send(Protocol.encodeCallActive())
                TelephonyManager.CALL_STATE_IDLE    -> OutboundSink.send(Protocol.encodeCallEnd())
                else                                -> {}   // RINGING -> incoming notif
            }
        }
    }

    @SuppressLint("MissingPermission")
    fun start() {
        if (tm == null) return
        if (ContextCompat.checkSelfPermission(app, Manifest.permission.READ_PHONE_STATE) !=
            PackageManager.PERMISSION_GRANTED) {
            Log.i(TAG, "READ_PHONE_STATE not granted; call-state sync disabled")
            return
        }
        try {
            tm.registerTelephonyCallback(app.mainExecutor, callback)
        } catch (t: Throwable) {
            Log.w(TAG, "registerTelephonyCallback failed: $t")
        }
    }

    fun stop() {
        try {
            tm?.unregisterTelephonyCallback(callback)
        } catch (_: Throwable) {
        }
    }

    private companion object {
        const val TAG = "CallStateTracker"
    }
}
