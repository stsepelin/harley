package com.vrodcluster.companion.notif

import android.app.Notification
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import com.vrodcluster.companion.ble.OutboundSink
import com.vrodcluster.companion.ble.Protocol

/**
 * Bridges Android notification posts/cancels to the cluster wire format.
 *
 * The system binds this service after the user grants notification
 * access (Settings → Apps → Special access → Notification access).
 * All classification logic lives in [NotifMapper]; the service only
 * extracts platform fields and forwards via [OutboundSink].
 */
class VrodNotifListener : NotificationListenerService() {

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
