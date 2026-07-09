package ee.zeppl.companion.media

import ee.zeppl.companion.ble.Protocol

/**
 * Pure decision logic for the media outbound stream. Takes a snapshot
 * of all currently active sessions plus the last-sent bytes, returns
 * either the next payload or null (nothing to send).
 *
 * Lives separately from [MediaWatcher] so the priority pick, allow-list
 * fallback, transient suppression, and debounce checks can be tested
 * on plain JUnit without the MediaSession framework.
 */
internal object MediaPublisher {

    /**
     * Plain-data view of one active session — what we extract from a
     * [android.media.session.MediaController] before any decisions get
     * made. Keeping the tuple instead of the MediaController itself is
     * what makes the rest of this module testable.
     */
    data class Source(
        val packageName: String,
        val stateCode:   Int,
        val artist:      String,
        val title:       String,
    )

    /**
     * Which session the rider most likely cares about: a currently
     * PLAYING source wins outright; else fall back to the first PAUSED.
     * Anything else (STOPPED / NONE / ERROR) is silent — the cluster
     * banner is cleared by the caller in that case.
     */
    fun pickActive(sources: List<Source>): Source? =
        sources.firstOrNull { it.stateCode == MediaMapper.STATE_PLAYING }
            ?: sources.firstOrNull { it.stateCode == MediaMapper.STATE_PAUSED }

    /**
     * The next payload to push, or null if nothing should go on the
     * wire. Drops on:
     *   - **track-transition transient** — picked source has non-STOPPED
     *     mapped state with empty artist+title. Spotify/YouTube Music
     *     flicker through this for ~1 s between tracks; forwarding it
     *     would briefly show "(unknown artist) / (unknown title)" on
     *     the cluster banner. STOPPED with empty fields is *not*
     *     suppressed — that's a real "media gone" signal.
     *   - **debounce** — identical bytes to the last push. PlaybackState
     *     position updates fire often and aren't user-visible.
     *
     * When the picked source's package is muted by the user we push
     * STOPPED so the cluster banner clears, *not* null — the rider has
     * just turned off Spotify, they don't want the previous track
     * lingering on the bike's screen.
     */
    fun nextPayload(
        sources:   List<Source>,
        isAllowed: (String) -> Boolean,
        lastBytes: ByteArray?,
    ): ByteArray? {
        val picked = pickActive(sources)
        val bytes  = if (picked == null || !isAllowed(picked.packageName)) {
            Protocol.encodeMedia(Protocol.MediaState.STOPPED, "", "")
        } else {
            val mapped = MediaMapper.toClusterState(picked.stateCode)
            if (mapped != Protocol.MediaState.STOPPED
             && picked.artist.isEmpty()
             && picked.title.isEmpty()
            ) return null
            MediaMapper.encode(picked.stateCode, picked.artist, picked.title)
        }
        if (lastBytes != null && bytes.contentEquals(lastBytes)) return null
        return bytes
    }
}
