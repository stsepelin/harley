package com.vrodcluster.companion.media

import com.vrodcluster.companion.ble.Protocol

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
     *   - identical bytes to the last push (debounce — PlaybackState
     *     position updates fire frequently and aren't user-visible),
     *   - a track-transition transient (state != STOPPED, empty
     *     artist + title — see [MediaMapper.encode]).
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
            MediaMapper.encode(picked.stateCode, picked.artist, picked.title)
                ?: return null
        }
        if (lastBytes != null && bytes.contentEquals(lastBytes)) return null
        return bytes
    }
}
