package ee.zeppl.companion.ble

import android.annotation.SuppressLint
import android.content.Context
import android.os.SystemClock
import android.telecom.TelecomManager
import android.util.Log
import android.view.KeyEvent
import ee.zeppl.companion.media.MediaWatcher
import ee.zeppl.companion.notif.ZepplNotifListener

/**
 * Dispatch the cluster-issued [ClientCommand]s into the platform APIs
 * that actually do something: TelecomManager for calls, the picked
 * MediaController for transport, the NotificationListener for dismiss.
 *
 * Stateless beyond the bound context — instantiated by [BleService]
 * and handed to [BleClient] as a callback. Runs on the BLE binder
 * thread; the platform APIs we touch are all binder-safe.
 */
class CommandHandler(private val context: Context) {

    fun dispatch(cmd: ClientCommand) {
        Log.i(TAG, "dispatch $cmd")
        when (cmd) {
            ClientCommand.CallAccept     -> acceptCall()
            // Reject = end-while-ringing; the platform doesn't distinguish.
            // Same path also covers in-call End — TelecomManager.endCall
            // tears down whatever the current call state is.
            ClientCommand.CallReject     -> endCall()
            ClientCommand.CallEnd        -> endCall()
            ClientCommand.MediaPrev      -> dispatchMedia(KeyEvent.KEYCODE_MEDIA_PREVIOUS)
            ClientCommand.MediaPlayPause -> dispatchMedia(KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE)
            ClientCommand.MediaNext      -> dispatchMedia(KeyEvent.KEYCODE_MEDIA_NEXT)
            is ClientCommand.NotifDismiss -> ZepplNotifListener.cancelByStableId(cmd.id)
        }
    }

    private fun dispatchMedia(keyCode: Int) {
        val controller = MediaWatcher.currentController
        if (controller == null) {
            Log.w(TAG, "media cmd $keyCode dropped — no active controller")
            return
        }
        // Two events: ACTION_DOWN + ACTION_UP. Apps that respond on key-up
        // (like Spotify's older versions) need both; sending only DOWN
        // makes them think a key is being held. Use SystemClock.uptimeMillis
        // as both downTime and eventTime, the standard idiom for synthetic
        // injection.
        val now = SystemClock.uptimeMillis()
        controller.dispatchMediaButtonEvent(KeyEvent(now, now, KeyEvent.ACTION_DOWN, keyCode, 0))
        controller.dispatchMediaButtonEvent(KeyEvent(now, now, KeyEvent.ACTION_UP,   keyCode, 0))
    }

    @SuppressLint("MissingPermission")
    private fun acceptCall() {
        // ANSWER_PHONE_CALLS — declared in the manifest and requested in
        // BleAccess.REQUIRED. If the user hasn't granted it the call goes
        // through to a SecurityException, which we log and drop.
        try {
            val tm = context.getSystemService(TelecomManager::class.java) ?: return
            tm.acceptRingingCall()
        } catch (e: SecurityException) {
            Log.w(TAG, "acceptRingingCall denied — ANSWER_PHONE_CALLS not granted", e)
        }
    }

    @SuppressLint("MissingPermission")
    private fun endCall() {
        try {
            val tm = context.getSystemService(TelecomManager::class.java) ?: return
            // endCall returns true if a call was ended, false otherwise.
            // We don't care about the result — the cluster has already
            // cleared its banner; this is fire-and-forget.
            tm.endCall()
        } catch (e: SecurityException) {
            Log.w(TAG, "endCall denied — ANSWER_PHONE_CALLS not granted", e)
        }
    }

    private companion object {
        const val TAG = "CommandHandler"
    }
}
