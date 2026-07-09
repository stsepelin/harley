package ee.zeppl.companion.ble

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.UUID

/**
 * Outbound wire format for the Zeppl cluster GATT characteristic.
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

    // GATT addresses — mirror `main/ble/ble_peripheral.c` on the cluster.
    // We borrow the Nordic UART Service layout (6E400001-...) because
    // every generic BLE explorer labels these as "RX / TX" for free
    // during early validation. The payload bytes are our own TLV.
    val SERVICE_UUID: UUID = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
    val RX_UUID:      UUID = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E")   // phone → cluster (write)
    val TX_UUID:      UUID = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E")   // cluster → phone (notify)

    // Mirrors phone_event_type_t.
    private const val TYPE_NOTIF         : Byte = 0x01
    private const val TYPE_NOTIF_DISMISS : Byte = 0x02
    private const val TYPE_MEDIA         : Byte = 0x03
    private const val TYPE_CONFIG        : Byte = 0x04
    private const val TYPE_ICON          : Byte = 0x05
    private const val TYPE_CALL_ACTIVE   : Byte = 0x06
    private const val TYPE_CALL_END      : Byte = 0x07

    // App-icon image geometry (mirrors the cluster): 48x48 RGB565, opaque.
    const val ICON_W = 48
    const val ICON_H = 48
    const val ICON_BYTES = ICON_W * ICON_H * 2

    // Mirrors notif_kind_t.
    enum class NotifKind(val wire: Byte) {
        CALL(0), SMS(1), APP(2);
    }

    // Mirrors media_state_t.
    enum class MediaState(val wire: Byte) {
        STOPPED(0), PAUSED(1), PLAYING(2);
    }

    /** A new (or replacing) notification. `iconId` (0 = none) references an icon
     *  previously streamed via [encodeIcon]; the cluster renders it if cached. */
    fun encodeNotif(
        id: UInt,
        kind: NotifKind,
        sender: String,
        message: String,
        iconId: UInt = 0u,
    ): ByteArray {
        val senderBytes  = truncatedUtf8(sanitize(sender),  Limits.NOTIF_SENDER_MAX)
        val messageBytes = truncatedUtf8(sanitize(message), Limits.NOTIF_MSG_MAX)
        // payload: u32 id, u8 kind, u8 sender_len, sender, u16 msg_len, message, u32 icon_id
        val payloadLen = 4 + 1 + 1 + senderBytes.size + 2 + messageBytes.size + 4
        val buf = ByteBuffer.allocate(3 + payloadLen).order(ByteOrder.LITTLE_ENDIAN)
        buf.put(TYPE_NOTIF)
        buf.putShort(payloadLen.toShort())
        buf.putInt(id.toInt())
        buf.put(kind.wire)
        buf.put(senderBytes.size.toByte())
        buf.put(senderBytes)
        buf.putShort(messageBytes.size.toShort())
        buf.put(messageBytes)
        buf.putInt(iconId.toInt())
        return buf.array()
    }

    /**
     * One chunk of an app-icon image (48x48 RGB565, opaque). Mirrors
     * `PHONE_EVT_ICON`: u32 icon_id, u16 total_len, u16 offset, chunk bytes.
     * The cluster reassembles chunks by offset into its icon cache.
     */
    fun encodeIcon(iconId: UInt, totalLen: Int, offset: Int, chunk: ByteArray): ByteArray {
        val payloadLen = 4 + 2 + 2 + chunk.size
        val buf = ByteBuffer.allocate(3 + payloadLen).order(ByteOrder.LITTLE_ENDIAN)
        buf.put(TYPE_ICON)
        buf.putShort(payloadLen.toShort())
        buf.putInt(iconId.toInt())
        buf.putShort(totalLen.toShort())
        buf.putShort(offset.toShort())
        buf.put(chunk)
        return buf.array()
    }

    /** Phone-side call went active (rider answered on the phone). No payload. */
    fun encodeCallActive(): ByteArray = byteArrayOf(TYPE_CALL_ACTIVE, 0x00, 0x00)

    /** Phone-side call ended (answered elsewhere / hung up / rejected). No payload. */
    fun encodeCallEnd(): ByteArray = byteArrayOf(TYPE_CALL_END, 0x00, 0x00)

    /** Dismiss a notification by id. The cluster ignores stale dismisses. */
    fun encodeDismiss(id: UInt): ByteArray {
        val buf = ByteBuffer.allocate(3 + 4).order(ByteOrder.LITTLE_ENDIAN)
        buf.put(TYPE_NOTIF_DISMISS)
        buf.putShort(4)
        buf.putInt(id.toInt())
        return buf.array()
    }

    /**
     * Config write-back to the cluster. Mirrors `PHONE_EVT_CONFIG` in
     * `firmware/main/phone/phone_protocol.c`: u16 speed_divisor (LE). The
     * cluster applies it live and persists it to NVS.
     */
    fun encodeConfig(speedDivisor: Int): ByteArray {
        val buf = ByteBuffer.allocate(3 + 2).order(ByteOrder.LITTLE_ENDIAN)
        buf.put(TYPE_CONFIG)
        buf.putShort(2)
        buf.putShort(speedDivisor.toShort())
        return buf.array()
    }

    /**
     * Now-playing snapshot. STOPPED clears the cluster's track entry and
     * pulls down the media banner if it was showing.
     */
    fun encodeMedia(state: MediaState, artist: String, title: String): ByteArray {
        val artistBytes = truncatedUtf8(sanitize(artist), Limits.MEDIA_FIELD_MAX)
        val titleBytes  = truncatedUtf8(sanitize(title),  Limits.MEDIA_FIELD_MAX)
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
     * Collapse embedded line breaks to a single space so the cluster
     * label can word-wrap on width alone. Everything else passes
     * through unchanged — emoji and other codepoints outside the
     * cluster font's range will render as boxes until a fallback emoji
     * font is added, but we'd rather show "something is here" than
     * silently drop content.
     */
    internal fun sanitize(s: String): String {
        val out = StringBuilder(s.length)
        var i = 0
        while (i < s.length) {
            val cp = s.codePointAt(i)
            when (cp) {
                0x09, 0x0A, 0x0D -> out.append(' ')
                else             -> out.appendCodePoint(cp)
            }
            i += Character.charCount(cp)
        }
        return out.toString()
    }

    /**
     * Truncate to fit the cluster's fixed buffer. The cluster reserves
     * one byte for the NUL terminator, so we send at most `max - 1`
     * bytes and the receiver appends `\0`. We truncate on a UTF-8
     * codepoint boundary so we never split a multi-byte sequence in
     * half — sanitize already drops non-renderable codepoints, but
     * Cyrillic is still 2-byte UTF-8.
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
