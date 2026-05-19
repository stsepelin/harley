package com.vrodcluster.companion.media

import com.vrodcluster.companion.ble.Protocol

/**
 * Pure mapping between [android.media.session.PlaybackState] state codes
 * and the cluster's tri-state media enum.
 *
 * Lives separately from [MediaWatcher] so the bucket boundaries can be
 * exercised on a plain JVM (no MediaSession framework needed). Constants
 * mirror PlaybackState.STATE_* and must stay in lock-step.
 */
internal object MediaMapper {

    // Mirrors android.media.session.PlaybackState.STATE_*.
    const val STATE_NONE       = 0
    const val STATE_STOPPED    = 1
    const val STATE_PAUSED     = 2
    const val STATE_PLAYING    = 3
    const val STATE_FAST_FWD   = 4
    const val STATE_REWINDING  = 5
    const val STATE_BUFFERING  = 6
    const val STATE_ERROR      = 7
    const val STATE_CONNECTING = 8

    /**
     * Bucket every Android state into PLAYING / PAUSED / STOPPED. We
     * count FAST_FORWARDING / REWINDING / BUFFERING as PLAYING because
     * the user is still mid-track — a brief BUFFERING blip shouldn't
     * pull the cluster's banner away. ERROR and CONNECTING collapse to
     * STOPPED so the cluster doesn't show "playing" while nothing is.
     */
    fun toClusterState(stateCode: Int): Protocol.MediaState = when (stateCode) {
        STATE_PLAYING, STATE_FAST_FWD, STATE_REWINDING, STATE_BUFFERING -> Protocol.MediaState.PLAYING
        STATE_PAUSED                                                    -> Protocol.MediaState.PAUSED
        else /* NONE / STOPPED / ERROR / CONNECTING */                  -> Protocol.MediaState.STOPPED
    }

    /**
     * Convenience for callers that have the raw triple. Returns null
     * when the snapshot looks like a track-transition transient:
     * Spotify (and others) flicker through PAUSED/PLAYING with empty
     * metadata for ~1s between tracks. Forwarding those would briefly
     * show "(unknown artist) / (unknown title)" on the cluster.
     *
     * STOPPED with empty fields is *not* suppressed — that's a real
     * "media gone" signal and the cluster needs it to clear its banner.
     */
    fun encode(stateCode: Int, artist: String, title: String): ByteArray? {
        val clusterState = toClusterState(stateCode)
        if (clusterState != Protocol.MediaState.STOPPED
         && artist.isEmpty()
         && title.isEmpty()
        ) return null
        return Protocol.encodeMedia(clusterState, artist, title)
    }
}
