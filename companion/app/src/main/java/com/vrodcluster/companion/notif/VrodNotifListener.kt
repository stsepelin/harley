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
        lastBytesById.clear()
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
        val id = NotifMapper.stableId(sbn.key)
        // Drop identical reposts. Messaging apps repost the same
        // notification on read receipts / typing indicators / "X is
        // online" updates — same title, same body, no rider-visible
        // change. Compare against the last wire bytes per id; identical
        // → skip the BLE write and the LRU bump. Saves a GATT write per
        // status-change ping (Telegram can fire one a second).
        val prev = lastBytesById[id]
        if (prev != null && prev.contentEquals(bytes)) return
        lastBytesById[id] = bytes
        // Track id → key so a cluster-side dismiss (swipe on the ride
        // screen → CMD_NOTIF_DISMISS over BLE) can find the SBN to
        // cancel. Only one entry per id is kept; if the upstream poster
        // recycles a key, the latest mapping wins.
        idToKey[id] = sbn.key
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
        lastBytesById.remove(id)
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
        // on post and pruned on remove. Bounded LRU: a busy messaging app
        // (Telegram thread updates, mail with hundreds of read receipts)
        // would otherwise grow this unbounded across the listener's
        // lifetime. accessOrder=true so a cluster-side dismiss bumps the
        // entry to the freshest end; eviction starts above 256 entries,
        // and the eviction loser is the least-recently-touched id — also
        // the one least likely to still be visible on the phone.
        //
        // synchronizedMap because LinkedHashMap's accessOrder reads
        // mutate internal state, so concurrent .get() callers would
        // otherwise race during eviction. ConcurrentHashMap can't do
        // bounded LRU on its own.
        private const val ID_KEY_MAX = 256
        private val idToKey: MutableMap<UInt, String> =
            java.util.Collections.synchronizedMap(
                object : LinkedHashMap<UInt, String>(64, 0.75f, /*accessOrder=*/true) {
                    override fun removeEldestEntry(
                        eldest: MutableMap.MutableEntry<UInt, String>,
                    ): Boolean = size > ID_KEY_MAX
                }
            )

        // Last wire bytes per id — paired with idToKey for the
        // repost-dedupe at the top of onNotificationPosted. Bounded the
        // same way so the dedupe state can't outgrow the dismiss map.
        private val lastBytesById: MutableMap<UInt, ByteArray> =
            java.util.Collections.synchronizedMap(
                object : LinkedHashMap<UInt, ByteArray>(64, 0.75f, /*accessOrder=*/true) {
                    override fun removeEldestEntry(
                        eldest: MutableMap.MutableEntry<UInt, ByteArray>,
                    ): Boolean = size > ID_KEY_MAX
                }
            )

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
