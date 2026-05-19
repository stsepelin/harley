package com.vrodcluster.companion.notif

import android.app.Notification
import android.content.ComponentName
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import com.vrodcluster.companion.ble.OutboundSink
import com.vrodcluster.companion.ble.Protocol
import com.vrodcluster.companion.media.MediaWatcher

/**
 * Bridges Android notification posts/cancels to the cluster wire format.
 * Also hosts [MediaWatcher] because MediaSessionManager auth piggybacks
 * on the notification-listener grant — one user-facing permission, two
 * data streams.
 *
 * The system binds this service after the user grants notification
 * access (Settings → Apps → Special access → Notification access).
 * All classification logic lives in [NotifMapper]; the service only
 * extracts platform fields and forwards via [OutboundSink].
 */
class VrodNotifListener : NotificationListenerService() {

    private val mediaWatcher = MediaWatcher()

    override fun onListenerConnected() {
        super.onListenerConnected()
        mediaWatcher.start(this, ComponentName(this, VrodNotifListener::class.java))
    }

    override fun onListenerDisconnected() {
        mediaWatcher.stop()
        super.onListenerDisconnected()
    }


    override fun onNotificationPosted(sbn: StatusBarNotification) {
        if (!AllowList.isAllowed(this, sbn.packageName)) return
        val extras = sbn.notification.extras
        val title  = extras.getCharSequence(Notification.EXTRA_TITLE)?.toString() ?: ""
        val text   = extras.getCharSequence(Notification.EXTRA_TEXT) ?.toString() ?: ""
        val bytes  = NotifMapper.encodePost(
            ownPackage  = packageName,
            packageName = sbn.packageName,
            isOngoing   = sbn.isOngoing,
            flags       = sbn.notification.flags,
            key         = sbn.key,
            category    = sbn.notification.category,
            template    = extras.getString(Notification.EXTRA_TEMPLATE),
            title       = title,
            text        = text,
        ) ?: return
        OutboundSink.send(bytes)
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification) {
        // Same mute + own-package filters as the post path. Without
        // them the cluster would see DISMISS ids it never received a
        // matching NOTIF for — harmless (the queue ignores unknown ids)
        // but pointless wire chatter.
        if (sbn.packageName == packageName) return
        if (!AllowList.isAllowed(this, sbn.packageName)) return
        OutboundSink.send(Protocol.encodeDismiss(NotifMapper.stableId(sbn.key)))
    }
}
