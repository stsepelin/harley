package ee.zeppl.companion.ble

/**
 * Cluster → phone command channel. Mirrors `firmware/main/phone/phone_protocol.{h,c}`
 * byte-for-byte. The cluster's encoder is the ground truth — see
 * `test_phone_protocol.c` (test_encode_*) for fixtures we cross-check
 * against. Touch this file → update both sides and re-run both suites.
 *
 * Framing matches the RX direction (type + LE u16 payload_len + payload),
 * but type bytes are in a different range so the two streams could share
 * a parser later without ambiguity:
 *   0x10..0x12 — call control
 *   0x20..0x22 — media transport
 *   0x30       — notif dismiss (u32 id)
 */
sealed class ClientCommand {
    data object CallAccept     : ClientCommand()
    data object CallReject     : ClientCommand()
    data object CallEnd        : ClientCommand()
    data object MediaPrev      : ClientCommand()
    data object MediaPlayPause : ClientCommand()
    data object MediaNext      : ClientCommand()
    data class  NotifDismiss(val id: UInt) : ClientCommand()
}

object ClientProtocol {

    private const val TYPE_CALL_ACCEPT     : Byte = 0x10
    private const val TYPE_CALL_REJECT     : Byte = 0x11
    private const val TYPE_CALL_END        : Byte = 0x12
    private const val TYPE_MEDIA_PREV      : Byte = 0x20
    private const val TYPE_MEDIA_PLAY_PAUSE: Byte = 0x21
    private const val TYPE_MEDIA_NEXT      : Byte = 0x22
    private const val TYPE_NOTIF_DISMISS   : Byte = 0x30

    /**
     * Decode one notification payload. Returns null on any framing
     * mismatch — the cluster framing is one message per BLE notify, so
     * we don't need to handle partial reads here. Treat null as "drop
     * this packet" and log at the caller.
     */
    fun decode(bytes: ByteArray): ClientCommand? {
        if (bytes.size < 3) return null
        val type       = bytes[0]
        val payloadLen = (bytes[1].toInt() and 0xFF) or
                         ((bytes[2].toInt() and 0xFF) shl 8)
        if (bytes.size < 3 + payloadLen) return null
        return when (type) {
            TYPE_CALL_ACCEPT      -> ClientCommand.CallAccept
            TYPE_CALL_REJECT      -> ClientCommand.CallReject
            TYPE_CALL_END         -> ClientCommand.CallEnd
            TYPE_MEDIA_PREV       -> ClientCommand.MediaPrev
            TYPE_MEDIA_PLAY_PAUSE -> ClientCommand.MediaPlayPause
            TYPE_MEDIA_NEXT       -> ClientCommand.MediaNext
            TYPE_NOTIF_DISMISS    -> {
                if (payloadLen != 4) return null
                val id = (bytes[3].toInt() and 0xFF)        or
                         ((bytes[4].toInt() and 0xFF) shl 8)  or
                         ((bytes[5].toInt() and 0xFF) shl 16) or
                         ((bytes[6].toInt() and 0xFF) shl 24)
                ClientCommand.NotifDismiss(id.toUInt())
            }
            else -> null
        }
    }
}
