package com.vrodcluster.companion.ble

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The contract between this app and the cluster is the byte layout
 * parsed by `main/phone/phone_protocol.c`. These tests assert the exact
 * bytes we'll put on the wire so a regression here is caught before any
 * device-side debugging.
 *
 * Fixtures mirror `test_apps/host/tests/test_phone_protocol.c`. If you
 * touch one side, touch both.
 */
class ProtocolTest {

    @Test fun `notif round-trip matches C test_parse_notif fixture`() {
        // C: id=0xABCD1234, kind=CALL, sender="John", message="ringing"
        val out = Protocol.encodeNotif(0xABCD1234u, Protocol.NotifKind.CALL, "John", "ringing")
        val expected = byteArrayOf(
            0x01,                                    // type = NOTIF
            0x13, 0x00,                              // payload_len = 19 LE (4+1+1+4+2+7)
            0x34.toByte(), 0x12, 0xCD.toByte(), 0xAB.toByte(),  // id LE
            0x00,                                    // kind = CALL
            0x04,                                    // sender_len
            'J'.code.toByte(), 'o'.code.toByte(), 'h'.code.toByte(), 'n'.code.toByte(),
            0x07, 0x00,                              // msg_len = 7 LE
            'r'.code.toByte(), 'i'.code.toByte(), 'n'.code.toByte(),
            'g'.code.toByte(), 'i'.code.toByte(), 'n'.code.toByte(), 'g'.code.toByte(),
        )
        assertArrayEquals(expected, out)
    }

    @Test fun `dismiss has fixed 4-byte payload`() {
        val out = Protocol.encodeDismiss(0x42u)
        val expected = byteArrayOf(
            0x02,                                    // type = DISMISS
            0x04, 0x00,                              // payload_len = 4 LE
            0x42, 0x00, 0x00, 0x00,                  // id LE
        )
        assertArrayEquals(expected, out)
    }

    @Test fun `media round-trip matches C test_parse_media fixture`() {
        val out = Protocol.encodeMedia(Protocol.MediaState.PLAYING, "Ramones", "Blitzkrieg Bop")
        val expected = byteArrayOf(
            0x03,                                    // type = MEDIA
            0x18, 0x00,                              // payload_len = 24 LE
            0x02,                                    // state = PLAYING
            0x07,                                    // artist_len
            'R'.code.toByte(), 'a'.code.toByte(), 'm'.code.toByte(),
            'o'.code.toByte(), 'n'.code.toByte(), 'e'.code.toByte(), 's'.code.toByte(),
            0x0E,                                    // title_len = 14
            'B'.code.toByte(), 'l'.code.toByte(), 'i'.code.toByte(), 't'.code.toByte(),
            'z'.code.toByte(), 'k'.code.toByte(), 'r'.code.toByte(), 'i'.code.toByte(),
            'e'.code.toByte(), 'g'.code.toByte(), ' '.code.toByte(),
            'B'.code.toByte(), 'o'.code.toByte(), 'p'.code.toByte(),
        )
        assertArrayEquals(expected, out)
    }

    @Test fun `empty media stopped is valid`() {
        val out = Protocol.encodeMedia(Protocol.MediaState.STOPPED, "", "")
        val expected = byteArrayOf(
            0x03,
            0x03, 0x00,        // payload_len = 3 LE (state + 2 zero lengths)
            0x00,              // state = STOPPED
            0x00,              // artist_len = 0
            0x00,              // title_len = 0
        )
        assertArrayEquals(expected, out)
    }

    // --- truncation -------------------------------------------------------

    @Test fun `sender longer than buffer is truncated to MAX-1`() {
        // Mirrors C's test_long_sender_is_truncated_to_buffer: a 48-byte
        // sender must be cut to 47 bytes on the wire (cluster appends NUL
        // in its 48-byte buffer).
        val longSender = "A".repeat(Protocol.Limits.NOTIF_SENDER_MAX)
        val out = Protocol.encodeNotif(1u, Protocol.NotifKind.APP, longSender, "x")

        // Locate sender_len byte (offset 3+4+1 = 8) and verify it equals MAX-1.
        val senderLen = out[8].toInt() and 0xFF
        assertEquals(Protocol.Limits.NOTIF_SENDER_MAX - 1, senderLen)
    }

    @Test fun `multibyte utf8 sender truncates on codepoint boundary`() {
        // 16 × "テ" (3 bytes each in UTF-8) = 48 bytes — exactly the cap.
        // We must not split a continuation byte off the last codepoint.
        // 47 bytes can't fit a whole "テ" boundary unless we walk back: we
        // should end up with 15 × 3 = 45 bytes, not 47.
        val s = "テ".repeat(16)
        val out = Protocol.encodeNotif(1u, Protocol.NotifKind.APP, s, "")
        val senderLen = out[8].toInt() and 0xFF
        // Whatever we cut to, it must be a multiple of 3 (since every char
        // is 3 bytes) and ≤ MAX-1.
        assertEquals(0, senderLen % 3)
        assertTrue(senderLen <= Protocol.Limits.NOTIF_SENDER_MAX - 1)
    }
}
