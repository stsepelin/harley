package com.vrodcluster.companion.ble

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Outbound wire format for the V-Rod cluster GATT characteristic.
 *
 * Mirrors `main/phone/phone_protocol.c` byte-for-byte. The cluster's
 * parser is the ground truth — see `test_phone_protocol.c` for the
 * fixtures we cross-check against. If you touch this file, update both
 * sides and re-run both test suites.
 *
 * Framing: u8 type, u16 payload_len (little-endian), payload.
 *
 * String fields are UTF-8. The cluster's fixed buffers truncate at
 * NOTIF_SENDER_MAX / NOTIF_MSG_MAX / MEDIA_FIELD_MAX (sized for the
 * banner widget — see [Limits]); over-long strings are *also* truncated
 * here so the receiver never sees padded or undelimited bytes.
 */
object Protocol {

    // Mirrors phone.h. Keep in lock-step.
    object Limits {
        const val NOTIF_SENDER_MAX = 48
        const val NOTIF_MSG_MAX    = 128
        const val MEDIA_FIELD_MAX  = 48
    }

    // Mirrors phone_event_type_t.
    private const val TYPE_NOTIF         : Byte = 0x01
    private const val TYPE_NOTIF_DISMISS : Byte = 0x02
    private const val TYPE_MEDIA         : Byte = 0x03

    // Mirrors notif_kind_t.
    enum class NotifKind(val wire: Byte) {
        CALL(0), SMS(1), APP(2);
    }

    // Mirrors media_state_t.
    enum class MediaState(val wire: Byte) {
        STOPPED(0), PAUSED(1), PLAYING(2);
    }

    /** A new (or replacing) notification. */
    fun encodeNotif(id: UInt, kind: NotifKind, sender: String, message: String): ByteArray {
        val senderBytes  = truncatedUtf8(sender,  Limits.NOTIF_SENDER_MAX)
        val messageBytes = truncatedUtf8(message, Limits.NOTIF_MSG_MAX)
        // payload: u32 id, u8 kind, u8 sender_len, sender, u16 msg_len, message
        val payloadLen = 4 + 1 + 1 + senderBytes.size + 2 + messageBytes.size
        val buf = ByteBuffer.allocate(3 + payloadLen).order(ByteOrder.LITTLE_ENDIAN)
        buf.put(TYPE_NOTIF)
        buf.putShort(payloadLen.toShort())
        buf.putInt(id.toInt())
        buf.put(kind.wire)
        buf.put(senderBytes.size.toByte())
        buf.put(senderBytes)
        buf.putShort(messageBytes.size.toShort())
        buf.put(messageBytes)
        return buf.array()
    }

    /** Dismiss a notification by id. The cluster ignores stale dismisses. */
    fun encodeDismiss(id: UInt): ByteArray {
        val buf = ByteBuffer.allocate(3 + 4).order(ByteOrder.LITTLE_ENDIAN)
        buf.put(TYPE_NOTIF_DISMISS)
        buf.putShort(4)
        buf.putInt(id.toInt())
        return buf.array()
    }

    /**
     * Now-playing snapshot. STOPPED clears the cluster's track entry and
     * pulls down the media banner if it was showing.
     */
    fun encodeMedia(state: MediaState, artist: String, title: String): ByteArray {
        val artistBytes = truncatedUtf8(artist, Limits.MEDIA_FIELD_MAX)
        val titleBytes  = truncatedUtf8(title,  Limits.MEDIA_FIELD_MAX)
        val payloadLen = 1 + 1 + artistBytes.size + 1 + titleBytes.size
        val buf = ByteBuffer.allocate(3 + payloadLen).order(ByteOrder.LITTLE_ENDIAN)
        buf.put(TYPE_MEDIA)
        buf.putShort(payloadLen.toShort())
        buf.put(state.wire)
        buf.put(artistBytes.size.toByte())
        buf.put(artistBytes)
        buf.put(titleBytes.size.toByte())
        buf.put(titleBytes)
        return buf.array()
    }

    /**
     * Truncate to fit the cluster's fixed buffer. The cluster reserves
     * one byte for the NUL terminator, so we send at most `max - 1`
     * bytes and the receiver appends `\0`. We truncate on a UTF-8
     * codepoint boundary so we never split a multi-byte sequence in
     * half (the cluster doesn't render emoji yet, but the buffer
     * shouldn't end with an orphan continuation byte regardless).
     */
    private fun truncatedUtf8(s: String, max: Int): ByteArray {
        val raw = s.toByteArray(Charsets.UTF_8)
        if (raw.size <= max - 1) return raw
        var cut = max - 1
        // Walk back to the last codepoint boundary. UTF-8 continuation
        // bytes are 10xxxxxx; valid starts are anything else.
        while (cut > 0 && (raw[cut].toInt() and 0xC0) == 0x80) cut--
        return raw.copyOf(cut)
    }
}
