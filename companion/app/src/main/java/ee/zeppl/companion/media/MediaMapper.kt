package ee.zeppl.companion.media

import ee.zeppl.companion.ble.Protocol

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

    /** Pure state-bucket → wire-bytes mapping. The "should this go on
     *  the wire at all?" question (transients, debounce, mute fallback)
     *  belongs to [MediaPublisher] — keeping it out of here is what lets
     *  this module be a one-line equivalence with [Protocol.encodeMedia]. */
    fun encode(stateCode: Int, artist: String, title: String): ByteArray =
        Protocol.encodeMedia(toClusterState(stateCode), artist, title)
}
