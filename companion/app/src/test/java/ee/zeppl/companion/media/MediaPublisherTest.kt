package ee.zeppl.companion.media

import ee.zeppl.companion.ble.Protocol
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * Priority pick + allowlist fallback + transient suppression + debounce.
 * Driven with plain [MediaPublisher.Source] tuples so the framework-side
 * MediaController never has to be constructed.
 */
class MediaPublisherTest {

    private val allowAll: (String) -> Boolean = { true }

    private fun src(
        pkg:    String = "com.spotify.music",
        state:  Int    = MediaMapper.STATE_PLAYING,
        artist: String = "Ramones",
        title:  String = "Blitzkrieg Bop",
    ) = MediaPublisher.Source(pkg, state, artist, title)

    private val stoppedClear: ByteArray =
        Protocol.encodeMedia(Protocol.MediaState.STOPPED, "", "")

    // --- pickActive priority -----------------------------------------------

    @Test fun `pickActive returns null on empty list`() {
        assertNull(MediaPublisher.pickActive(emptyList()))
    }

    @Test fun `pickActive prefers PLAYING over PAUSED`() {
        val paused  = src(pkg = "a", state = MediaMapper.STATE_PAUSED)
        val playing = src(pkg = "b", state = MediaMapper.STATE_PLAYING)
        // Order matters: playing arrives second to ensure we don't just
        // pick by position.
        assertEquals(playing, MediaPublisher.pickActive(listOf(paused, playing)))
    }

    @Test fun `pickActive picks first PAUSED when nothing is PLAYING`() {
        val a = src(pkg = "a", state = MediaMapper.STATE_PAUSED)
        val b = src(pkg = "b", state = MediaMapper.STATE_PAUSED)
        assertEquals(a, MediaPublisher.pickActive(listOf(a, b)))
    }

    @Test fun `pickActive returns null when nothing is playing or paused`() {
        // STATE_NONE / STOPPED / ERROR sessions are still listed by the
        // platform but don't count as active.
        val stopped = src(state = MediaMapper.STATE_STOPPED)
        val none    = src(state = MediaMapper.STATE_NONE)
        assertNull(MediaPublisher.pickActive(listOf(stopped, none)))
    }

    @Test fun `pickActive picks PLAYING even when listed after STOPPED`() {
        val stopped = src(pkg = "a", state = MediaMapper.STATE_STOPPED)
        val playing = src(pkg = "b", state = MediaMapper.STATE_PLAYING)
        assertEquals(playing, MediaPublisher.pickActive(listOf(stopped, playing)))
    }

    @Test fun `buffering counts as PLAYING for priority pick`() {
        // MediaMapper buckets BUFFERING into PLAYING for the cluster
        // banner, but pickActive uses the raw state code. STATE_BUFFERING
        // is *not* STATE_PLAYING, so a single buffering session is
        // currently treated as not active. Verify the boundary so a
        // future refactor that wants to change this is forced to think
        // about it.
        val buffering = src(state = MediaMapper.STATE_BUFFERING)
        assertNull(MediaPublisher.pickActive(listOf(buffering)))
    }

    // --- nextPayload happy path -------------------------------------------

    @Test fun `nextPayload encodes the picked source`() {
        val out  = MediaPublisher.nextPayload(
            sources   = listOf(src(state = MediaMapper.STATE_PLAYING, artist = "X", title = "Y")),
            isAllowed = allowAll,
            lastBytes = null,
        )
        val want = Protocol.encodeMedia(Protocol.MediaState.PLAYING, "X", "Y")
        assertArrayEquals(want, out)
    }

    @Test fun `empty sources push a STOPPED clear`() {
        val out = MediaPublisher.nextPayload(
            sources   = emptyList(),
            isAllowed = allowAll,
            lastBytes = null,
        )
        assertArrayEquals(stoppedClear, out)
    }

    @Test fun `no PLAYING or PAUSED pushes a STOPPED clear`() {
        val out = MediaPublisher.nextPayload(
            sources   = listOf(src(state = MediaMapper.STATE_STOPPED)),
            isAllowed = allowAll,
            lastBytes = null,
        )
        assertArrayEquals(stoppedClear, out)
    }

    // --- debounce ----------------------------------------------------------

    @Test fun `repeat publish with identical bytes returns null`() {
        val sources  = listOf(src(artist = "Hiko", title = "Ghost"))
        val first    = MediaPublisher.nextPayload(sources, allowAll, lastBytes = null)
        assertNotNull(first)
        val repeat   = MediaPublisher.nextPayload(sources, allowAll, lastBytes = first)
        assertNull("debounce should swallow identical bytes", repeat)
    }

    @Test fun `state change after PLAYING pushes a new payload`() {
        val playing = MediaPublisher.nextPayload(
            sources   = listOf(src(state = MediaMapper.STATE_PLAYING, artist = "A", title = "B")),
            isAllowed = allowAll,
            lastBytes = null,
        )
        val paused = MediaPublisher.nextPayload(
            sources   = listOf(src(state = MediaMapper.STATE_PAUSED,  artist = "A", title = "B")),
            isAllowed = allowAll,
            lastBytes = playing,
        )
        assertNotNull(paused)
    }

    @Test fun `repeat STOPPED clear is debounced`() {
        val first = MediaPublisher.nextPayload(emptyList(), allowAll, lastBytes = null)
        val again = MediaPublisher.nextPayload(emptyList(), allowAll, lastBytes = first)
        assertNull(again)
    }

    // --- transient suppression --------------------------------------------

    @Test fun `PLAYING with empty metadata is suppressed (no STOPPED fallback)`() {
        // This is the Spotify track-skip case — empty metadata briefly
        // appears on PLAYING. The publisher must wait for proper data
        // rather than fall back to a clear, otherwise the cluster blinks
        // STOPPED between every track.
        val out = MediaPublisher.nextPayload(
            sources   = listOf(src(state = MediaMapper.STATE_PLAYING, artist = "", title = "")),
            isAllowed = allowAll,
            lastBytes = null,
        )
        assertNull(out)
    }

    @Test fun `PAUSED with empty metadata is suppressed`() {
        val out = MediaPublisher.nextPayload(
            sources   = listOf(src(state = MediaMapper.STATE_PAUSED, artist = "", title = "")),
            isAllowed = allowAll,
            lastBytes = null,
        )
        assertNull(out)
    }

    // --- allowlist fallback -----------------------------------------------

    @Test fun `muted source produces a STOPPED clear, not a publish`() {
        // User muted Spotify mid-playback. The cluster banner should
        // clear so the rider doesn't see a stale track lingering.
        val out = MediaPublisher.nextPayload(
            sources   = listOf(src(pkg = "com.spotify.music", state = MediaMapper.STATE_PLAYING)),
            isAllowed = { false },
            lastBytes = null,
        )
        assertArrayEquals(stoppedClear, out)
    }

    @Test fun `muted source with last STOPPED bytes returns null (debounce)`() {
        val out = MediaPublisher.nextPayload(
            sources   = listOf(src(pkg = "com.spotify.music")),
            isAllowed = { false },
            lastBytes = stoppedClear,
        )
        assertNull(out)
    }

    @Test fun `allowlist predicate is called with the picked package name`() {
        val seen = mutableListOf<String>()
        MediaPublisher.nextPayload(
            sources   = listOf(src(pkg = "com.example.player")),
            isAllowed = { seen += it; true },
            lastBytes = null,
        )
        // Only the picked one is consulted; uninvolved packages aren't
        // queried, which keeps the SharedPrefs reads to one per publish.
        assertEquals(listOf("com.example.player"), seen)
    }

    @Test fun `allowlist not consulted when no source is picked`() {
        // pickActive returned null — there's nothing to mute, so we
        // shouldn't touch the allowlist at all.
        val seen = mutableListOf<String>()
        MediaPublisher.nextPayload(
            sources   = emptyList(),
            isAllowed = { seen += it; true },
            lastBytes = null,
        )
        assertEquals(emptyList<String>(), seen)
    }
}
