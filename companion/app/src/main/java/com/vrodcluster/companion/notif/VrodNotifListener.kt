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
        instance = this
        mediaWatcher.start(this, ComponentName(this, VrodNotifListener::class.java))
    }

    override fun onListenerDisconnected() {
        mediaWatcher.stop()
        instance = null
        idToKey.clear()
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
        // Track id → key so a cluster-side dismiss (swipe on the ride
        // screen → CMD_NOTIF_DISMISS over BLE) can find the SBN to
        // cancel. Only one entry per id is kept; if the upstream poster
        // recycles a key, the latest mapping wins.
        idToKey[NotifMapper.stableId(sbn.key)] = sbn.key
        OutboundSink.send(bytes)
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification) {
        // Same mute + own-package filters as the post path. Without
        // them the cluster would see DISMISS ids it never received a
        // matching NOTIF for — harmless (the queue ignores unknown ids)
        // but pointless wire chatter.
        if (sbn.packageName == packageName) return
        if (!AllowList.isAllowed(this, sbn.packageName)) return
        val id = NotifMapper.stableId(sbn.key)
        idToKey.remove(id)
        OutboundSink.send(Protocol.encodeDismiss(id))
    }

    companion object {
        // The active listener instance, set in onListenerConnected. The
        // BLE command handler reads this to dismiss a notification when
        // the cluster fires CMD_NOTIF_DISMISS. Null when the listener
        // isn't bound (user hasn't granted notification access yet, or
        // OS killed the service).
        @Volatile private var instance: VrodNotifListener? = null

        // Reverse map of NotifMapper.stableId(sbn.key) → sbn.key, populated
        // on post and pruned on remove. Lives in the companion object so
        // it survives the listener being torn down briefly (e.g. process
        // restart). Bounded only by the rate of unique notifications;
        // the listener disconnect path clears it.
        private val idToKey = java.util.concurrent.ConcurrentHashMap<UInt, String>()

        // Cluster-initiated dismiss. The cluster identifies the notif by
        // the same stableId(sbn.key) hash it received on post, and we
        // map back to the platform key to call cancelNotification. Quiet
        // no-op if the listener isn't bound or the id is stale (already
        // dismissed on the phone side).
        fun cancelByStableId(id: UInt) {
            val key = idToKey[id] ?: return
            instance?.cancelNotification(key)
        }
    }
}
