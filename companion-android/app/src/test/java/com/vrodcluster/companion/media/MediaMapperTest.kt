package com.vrodcluster.companion.media

import com.vrodcluster.companion.ble.Protocol
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * PlaybackState → cluster MediaState bucketing. Exercises every Android
 * state code so a future MediaWatcher refactor can't silently drop a
 * bucket boundary. Lives on plain JUnit since MediaMapper is pure.
 */
class MediaMapperTest {

    // --- state bucketing -------------------------------------------------

    @Test fun `STATE_PLAYING maps to PLAYING`() {
        assertEquals(Protocol.MediaState.PLAYING, MediaMapper.toClusterState(MediaMapper.STATE_PLAYING))
    }

    @Test fun `mid-track transitions count as PLAYING`() {
        // Buffer / scrub events fire as transient state changes; we
        // don't want them to yank the cluster's banner away.
        assertEquals(Protocol.MediaState.PLAYING, MediaMapper.toClusterState(MediaMapper.STATE_FAST_FWD))
        assertEquals(Protocol.MediaState.PLAYING, MediaMapper.toClusterState(MediaMapper.STATE_REWINDING))
        assertEquals(Protocol.MediaState.PLAYING, MediaMapper.toClusterState(MediaMapper.STATE_BUFFERING))
    }

    @Test fun `PAUSED maps to PAUSED`() {
        assertEquals(Protocol.MediaState.PAUSED, MediaMapper.toClusterState(MediaMapper.STATE_PAUSED))
    }

    @Test fun `inactive and error states collapse to STOPPED`() {
        // STATE_NONE: nothing has happened yet on this session.
        // STATE_STOPPED: deliberate stop.
        // STATE_ERROR: playback failure.
        // STATE_CONNECTING: cast/streaming handshake — nothing actually
        // playing yet.
        assertEquals(Protocol.MediaState.STOPPED, MediaMapper.toClusterState(MediaMapper.STATE_NONE))
        assertEquals(Protocol.MediaState.STOPPED, MediaMapper.toClusterState(MediaMapper.STATE_STOPPED))
        assertEquals(Protocol.MediaState.STOPPED, MediaMapper.toClusterState(MediaMapper.STATE_ERROR))
        assertEquals(Protocol.MediaState.STOPPED, MediaMapper.toClusterState(MediaMapper.STATE_CONNECTING))
    }

    @Test fun `unknown future state falls back to STOPPED`() {
        // If Android adds STATE_X = 9 we want the cluster to see
        // STOPPED rather than guess. Safer to be silent.
        assertEquals(Protocol.MediaState.STOPPED, MediaMapper.toClusterState(99))
    }

    // --- encode end-to-end -----------------------------------------------

    @Test fun `encode produces same bytes as Protocol_encodeMedia for PLAYING`() {
        val out  = MediaMapper.encode(MediaMapper.STATE_PLAYING, "Ramones", "Blitzkrieg Bop")
        val want = Protocol.encodeMedia(Protocol.MediaState.PLAYING, "Ramones", "Blitzkrieg Bop")
        assertArrayEquals(want, out)
    }

    @Test fun `encode of STOPPED with empty strings is a clear-track payload`() {
        val out  = MediaMapper.encode(MediaMapper.STATE_STOPPED, "", "")
        // type 0x03 (MEDIA), payload_len = 3 (state + two zero lengths).
        val want = byteArrayOf(0x03, 0x03, 0x00, 0x00, 0x00, 0x00)
        assertArrayEquals(want, out)
    }

    @Test fun `buffering with metadata still pushes the track`() {
        // Cluster should keep showing the track while Spotify rebuffers.
        val out  = MediaMapper.encode(MediaMapper.STATE_BUFFERING, "X", "Y")
        val want = Protocol.encodeMedia(Protocol.MediaState.PLAYING, "X", "Y")
        assertArrayEquals(want, out)
    }
}
