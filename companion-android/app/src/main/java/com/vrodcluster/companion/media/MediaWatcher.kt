package com.vrodcluster.companion.media

import android.content.ComponentName
import android.content.Context
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import com.vrodcluster.companion.ble.OutboundSink
import com.vrodcluster.companion.notif.AllowList

/**
 * Owns the lifecycle of [MediaSessionManager] subscriptions and pushes
 * a single, debounced "now playing" snapshot through [OutboundSink].
 *
 * Several media sessions can be active simultaneously (Spotify +
 * navigation voice + Chrome video). We pick the one most likely to be
 * what the rider cares about: a session currently PLAYING wins, else
 * the first PAUSED. If nothing qualifies — or the picked source's
 * package is muted — we push STOPPED so the cluster clears its banner.
 *
 * Hosted by [com.vrodcluster.companion.notif.VrodNotifListener] because
 * MediaSessionManager.getActiveSessions() requires a registered
 * NotificationListener component as the auth token; sharing the
 * service also keeps the binding count to one.
 */
class MediaWatcher {

    private var context:           Context? = null
    private var msm:               MediaSessionManager? = null
    private var sessionsListener:  MediaSessionManager.OnActiveSessionsChangedListener? = null
    private val controllerCbs    = mutableMapOf<MediaController, MediaController.Callback>()
    private var lastBytes:         ByteArray? = null

    fun start(context: Context, listenerComponent: ComponentName) {
        // Defensive: Android's contract pairs onListenerConnected /
        // onListenerDisconnected, but a re-bind glitch (or test harness
        // that calls start twice) would leak a sessions listener
        // otherwise. Re-entrant start is a no-op.
        if (sessionsListener != null) return

        this.context = context
        val mgr = context.getSystemService(Context.MEDIA_SESSION_SERVICE) as MediaSessionManager
        msm = mgr
        val l = MediaSessionManager.OnActiveSessionsChangedListener { active ->
            syncControllers(active ?: emptyList())
            publish()
        }
        mgr.addOnActiveSessionsChangedListener(l, listenerComponent)
        sessionsListener = l
        syncControllers(mgr.getActiveSessions(listenerComponent))
        publish()
    }

    fun stop() {
        msm?.let { mgr -> sessionsListener?.let { mgr.removeOnActiveSessionsChangedListener(it) } }
        sessionsListener = null
        controllerCbs.forEach { (c, cb) -> c.unregisterCallback(cb) }
        controllerCbs.clear()
        msm        = null
        lastBytes  = null
        context    = null
    }

    // --- internals -------------------------------------------------------

    private fun syncControllers(active: List<MediaController>) {
        val activeSet = active.toSet()
        // Unregister callbacks for controllers that left the active list.
        controllerCbs.keys.toList()
            .filter { it !in activeSet }
            .forEach { c -> controllerCbs.remove(c)?.let { c.unregisterCallback(it) } }
        // Register for new arrivals.
        active.forEach { c ->
            if (c !in controllerCbs) {
                val cb = object : MediaController.Callback() {
                    override fun onMetadataChanged(metadata: MediaMetadata?)      = publish()
                    override fun onPlaybackStateChanged(state: PlaybackState?)    = publish()
                    override fun onSessionDestroyed() {
                        // The framework also notifies via the sessions listener,
                        // but cleaning up locally avoids a stale entry between
                        // the destroy and the next active-sessions update.
                        controllerCbs.remove(c)
                        publish()
                    }
                }
                c.registerCallback(cb)
                controllerCbs[c] = cb
            }
        }
    }

    private fun publish() {
        val ctx     = context ?: return
        val sources = controllerCbs.keys.map { c ->
            MediaPublisher.Source(
                packageName = c.packageName,
                stateCode   = c.playbackState?.state ?: MediaMapper.STATE_NONE,
                artist      = c.metadata?.getString(MediaMetadata.METADATA_KEY_ARTIST) ?: "",
                title       = c.metadata?.getString(MediaMetadata.METADATA_KEY_TITLE)  ?: "",
            )
        }
        val bytes = MediaPublisher.nextPayload(
            sources   = sources,
            isAllowed = { AllowList.isAllowed(ctx, it) },
            lastBytes = lastBytes,
        ) ?: return
        lastBytes = bytes
        OutboundSink.send(bytes)
    }
}
