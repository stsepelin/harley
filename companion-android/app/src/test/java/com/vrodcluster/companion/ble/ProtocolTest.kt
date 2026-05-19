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
        // MAX-1 = 47 bytes is the budget; the continuation walk must
        // back up past the half-encoded "テ" landing us at 15 × 3 = 45.
        // Pinned exact (not just `% 3 == 0`) so a regression in the
        // back-walk surfaces precisely instead of producing some other
        // multiple of three.
        val s = "テ".repeat(16)
        val out = Protocol.encodeNotif(1u, Protocol.NotifKind.APP, s, "")
        val senderLen = out[8].toInt() and 0xFF
        assertEquals(45, senderLen)
    }

    @Test fun `ascii sender exactly at MAX-1 bytes is not truncated further`() {
        // 47 ASCII chars = 47 bytes — the budget exactly. No back-walk
        // should fire because byte 48 (the cut point) would be a fresh
        // codepoint start, not a continuation.
        val s = "A".repeat(Protocol.Limits.NOTIF_SENDER_MAX - 1)
        val out = Protocol.encodeNotif(1u, Protocol.NotifKind.APP, s, "")
        val senderLen = out[8].toInt() and 0xFF
        assertEquals(Protocol.Limits.NOTIF_SENDER_MAX - 1, senderLen)
    }

    // --- u32 id round-trip --------------------------------------------------

    @Test fun `dismiss id 0 round-trips as four zero bytes`() {
        val out = Protocol.encodeDismiss(0u)
        val expected = byteArrayOf(0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00)
        assertArrayEquals(expected, out)
    }

    @Test fun `dismiss id at u32 max round-trips correctly`() {
        // 0xFFFFFFFF would be -1 as a signed Int; Kotlin's UInt → Int
        // bit reinterpretation needs to put 0xFF in all four bytes,
        // not sign-extend or wrap.
        val out = Protocol.encodeDismiss(UInt.MAX_VALUE)
        val expected = byteArrayOf(
            0x02, 0x04, 0x00,
            0xFF.toByte(), 0xFF.toByte(), 0xFF.toByte(), 0xFF.toByte(),
        )
        assertArrayEquals(expected, out)
    }

    @Test fun `notif id with sign bit set round-trips correctly`() {
        // 0x80000000 — first bit set. A naive Int.toByteArray would
        // include a sign-extended leading byte; our LE u32 must stay
        // exactly four bytes.
        val out = Protocol.encodeNotif(0x80000000u, Protocol.NotifKind.APP, "", "")
        // Header: type(1) + payload_len(2). payload: u32 id + u8 kind +
        // u8 slen + 0 sender + u16 mlen + 0 msg = 8 bytes.
        // Bytes 3..6 are the id LE.
        assertEquals(0x00.toByte(), out[3])
        assertEquals(0x00.toByte(), out[4])
        assertEquals(0x00.toByte(), out[5])
        assertEquals(0x80.toByte(), out[6])
    }
}
