package ee.zeppl.companion.notif

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.provider.Settings

/**
 * Helpers around Android's notification-access grant.
 *
 * The grant is per-component, not per-app, and is stored in
 * `Settings.Secure.enabled_notification_listeners` as a colon-separated
 * list of flattened ComponentNames. There's no Activity-Result API for
 * it — we send the user to Settings and re-check on resume.
 */
object NotifAccess {

    /** True if [ZepplNotifListener] appears in the enabled-listeners list. */
    fun isGranted(context: Context): Boolean {
        val ours = ComponentName(context, ZepplNotifListener::class.java)
                       .flattenToString()
        val list = Settings.Secure.getString(
            context.contentResolver,
            "enabled_notification_listeners"
        ) ?: return false
        return list.split(':').any { it == ours }
    }

    /** Intent to the system Notification-Access settings page. */
    fun grantIntent(): Intent =
        Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
}
