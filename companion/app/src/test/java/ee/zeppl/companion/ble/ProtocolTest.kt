package ee.zeppl.companion.ble

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
            0x17, 0x00,                              // payload_len = 23 LE (4+1+1+4+2+7+4)
            0x34.toByte(), 0x12, 0xCD.toByte(), 0xAB.toByte(),  // id LE
            0x00,                                    // kind = CALL
            0x04,                                    // sender_len
            'J'.code.toByte(), 'o'.code.toByte(), 'h'.code.toByte(), 'n'.code.toByte(),
            0x07, 0x00,                              // msg_len = 7 LE
            'r'.code.toByte(), 'i'.code.toByte(), 'n'.code.toByte(),
            'g'.code.toByte(), 'i'.code.toByte(), 'n'.code.toByte(), 'g'.code.toByte(),
            0x00, 0x00, 0x00, 0x00,                  // icon_id = 0 (none)
        )
        assertArrayEquals(expected, out)
    }

    @Test fun `notif carries a trailing icon_id`() {
        val out = Protocol.encodeNotif(1u, Protocol.NotifKind.APP, "A", "b", iconId = 0xDEADBEEFu)
        // last four bytes are icon_id LE
        val tail = out.copyOfRange(out.size - 4, out.size)
        assertArrayEquals(
            byteArrayOf(0xEF.toByte(), 0xBE.toByte(), 0xAD.toByte(), 0xDE.toByte()),
            tail,
        )
    }

    @Test fun `encodes call active and end`() {
        assertArrayEquals(byteArrayOf(0x06, 0x00, 0x00), Protocol.encodeCallActive())
        assertArrayEquals(byteArrayOf(0x07, 0x00, 0x00), Protocol.encodeCallEnd())
    }

    @Test fun `encodes an icon chunk`() {
        val out = Protocol.encodeIcon(0x87654321u, totalLen = 4608, offset = 64,
                                      chunk = byteArrayOf(0xAA.toByte(), 0xBB.toByte()))
        val expected = byteArrayOf(
            0x05,                                    // type = ICON
            0x0A, 0x00,                              // payload_len = 10 LE (4+2+2+2)
            0x21, 0x43, 0x65, 0x87.toByte(),         // icon_id LE
            0x00, 0x12,                              // total_len = 4608 LE
            0x40, 0x00,                              // offset = 64 LE
            0xAA.toByte(), 0xBB.toByte(),            // chunk
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
        // 24 × Cyrillic "Я" (2 bytes each in UTF-8) = 48 bytes — exactly
        // the cap. MAX-1 = 47 bytes is the budget; the continuation walk
        // must back up past the half-encoded "Я" landing us at 23 × 2 = 46.
        // Cyrillic was chosen over Japanese here because sanitize() now
        // strips codepoints outside the cluster's font range, and the
        // basic Cyrillic block (0x0400-0x045F) survives — Hiragana would
        // be dropped entirely, defeating the truncation check.
        val s = "Я".repeat(24)
        val out = Protocol.encodeNotif(1u, Protocol.NotifKind.APP, s, "")
        val senderLen = out[8].toInt() and 0xFF
        assertEquals(46, senderLen)
    }

    @Test fun `sanitize keeps emoji on the wire`() {
        // Cluster's JBM font can't render them yet — they'll show as a
        // box until a fallback emoji font lands — but better visible-but-
        // ugly than silently dropped. Round-trips identically.
        assertEquals("hi 😀", Protocol.sanitize("hi 😀"))
    }

    @Test fun `sanitize keeps ascii cyrillic and degree`() {
        assertEquals("Привет 25°C", Protocol.sanitize("Привет 25°C"))
    }

    @Test fun `sanitize collapses control whitespace to spaces`() {
        // Tab / newline / CR collapse to a single space — the cluster
        // word-wraps on its own and doesn't honour embedded line breaks.
        assertEquals("a b c d", Protocol.sanitize("a\tb\nc\rd"))
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

    // --- UUID byte-shape vs firmware -------------------------------------

    @Test fun `service UUID bytes match the firmware's BLE_UUID128_INIT order`() {
        // The firmware writes:
        //   BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
        //                    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e)
        // NimBLE stores 128-bit UUIDs little-endian, so the macro bytes
        // are the reverse of the canonical string form.
        // Reading them MSB-first → 6E400001-B5A3-F393-E0A9-E50E24DCCA9E.
        // This test pins that round-trip so a swap on either side fails
        // loudly instead of producing a different service on the air.
        val expectedLe = byteArrayOf(
            0x9e.toByte(), 0xca.toByte(), 0xdc.toByte(), 0x24.toByte(),
            0x0e.toByte(), 0xe5.toByte(), 0xa9.toByte(), 0xe0.toByte(),
            0x93.toByte(), 0xf3.toByte(), 0xa3.toByte(), 0xb5.toByte(),
            0x01.toByte(), 0x00.toByte(), 0x40.toByte(), 0x6e.toByte(),
        )
        assertArrayEquals(expectedLe, uuidToLeBytes(Protocol.SERVICE_UUID))
    }

    @Test fun `RX UUID differs from service by the last-payload byte`() {
        // RX = SVC but with the second-from-end byte = 0x02. This is
        // the only byte the firmware's NUS_UUID_BYTES macro varies.
        val svc = uuidToLeBytes(Protocol.SERVICE_UUID)
        val rx  = uuidToLeBytes(Protocol.RX_UUID)
        // Bytes 12 differ: 0x01 → 0x02.
        assertEquals(0x01.toByte(), svc[12])
        assertEquals(0x02.toByte(), rx[12])
        // Everything else identical.
        for (i in 0 until 16) if (i != 12) assertEquals("byte $i", svc[i], rx[i])
    }

    @Test fun `TX UUID is RX with the variant byte stepped to 0x03`() {
        val tx = uuidToLeBytes(Protocol.TX_UUID)
        assertEquals(0x03.toByte(), tx[12])
    }

    /** Serialise a UUID to its on-wire little-endian byte form (the
     *  order NimBLE expects, and the order BLE_UUID128_INIT lists). */
    private fun uuidToLeBytes(u: java.util.UUID): ByteArray {
        val msb = u.mostSignificantBits
        val lsb = u.leastSignificantBits
        val be  = ByteArray(16)
        for (i in 0 until 8) be[i]     = ((msb ushr (8 * (7 - i))) and 0xFF).toByte()
        for (i in 0 until 8) be[8 + i] = ((lsb ushr (8 * (7 - i))) and 0xFF).toByte()
        return be.reversedArray()
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

    @Test fun `encodes config speed divisor`() {
        // Field-keyed: type, len=4, {id=1, len=2, u16 188}. Mirrors
        // test_phone_protocol.c::test_parse_config_speed_divisor.
        assertArrayEquals(
            byteArrayOf(0x04, 0x04, 0x00, 0x01, 0x02, 0xBC.toByte(), 0x00),
            Protocol.encodeConfig(speedDivisor = 188),
        )
    }

    @Test fun `encodes config layout`() {
        // {id=2, len=1, u8 1}. Mirrors test_parse_config_layout.
        assertArrayEquals(
            byteArrayOf(0x04, 0x03, 0x00, 0x02, 0x01, 0x01),
            Protocol.encodeConfig(layout = Protocol.LAYOUT_MAP),
        )
    }

    @Test fun `encodes config both fields`() {
        assertArrayEquals(
            byteArrayOf(0x04, 0x07, 0x00, 0x01, 0x02, 0xC8.toByte(), 0x00, 0x02, 0x01, 0x00),
            Protocol.encodeConfig(speedDivisor = 200, layout = Protocol.LAYOUT_CLASSIC),
        )
    }

    @Test fun `encodes a location fix`() {
        // Mirrors test_phone_protocol.c::test_parse_location. 59.482968,24.850976
        // -> lat_e7 594829680, lon_e7 248509760; heading 12345 cd.
        val b = Protocol.encodeLocation(59.482968, 24.850976, 12345)
        val buf = java.nio.ByteBuffer.wrap(b).order(java.nio.ByteOrder.LITTLE_ENDIAN)
        assertEquals(0x08.toByte(), buf.get())      // type
        assertEquals(10.toShort(), buf.short)        // payload_len
        assertEquals(594829680, buf.int)             // lat_e7
        assertEquals(248509760, buf.int)             // lon_e7
        assertEquals(12345.toShort(), buf.short)     // heading_cd
    }

    @Test fun `location heading unknown round-trips as 0xFFFF`() {
        val b = Protocol.encodeLocation(0.0, 0.0, Protocol.HEADING_UNKNOWN)
        val buf = java.nio.ByteBuffer.wrap(b).order(java.nio.ByteOrder.LITTLE_ENDIAN)
        buf.position(11)
        assertEquals(0xFFFF.toShort(), buf.short)
    }
}
