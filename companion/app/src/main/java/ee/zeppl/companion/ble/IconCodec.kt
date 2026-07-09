package ee.zeppl.companion.ble

/**
 * Pure helpers for the app-icon transfer: a stable per-app id, and converting a
 * decoded ARGB image into the opaque RGB565 bytes the cluster renders. The
 * Android bitmap extraction lives in [IconSender]; this part is unit-tested.
 */
object IconCodec {

    /** The cluster banner background — icons are composited over it (opaque). */
    const val BANNER_BG = 0x1A1A1A

    /**
     * Stable non-zero id for a package name (keys the cluster's icon cache).
     * String.hashCode is deterministic; remap 0 so it never collides with the
     * "no icon" sentinel.
     */
    fun iconId(packageName: String): UInt {
        val h = packageName.hashCode().toUInt()
        return if (h == 0u) 1u else h
    }

    /**
     * Composite an ARGB pixel array over [bg] (opaque) and pack to little-endian
     * RGB565 — 2 bytes per pixel, matching the cluster's LVGL RGB565 surface.
     */
    fun argbToRgb565Opaque(argb: IntArray, bg: Int = BANNER_BG): ByteArray {
        val bgR = (bg ushr 16) and 0xFF
        val bgG = (bg ushr 8) and 0xFF
        val bgB = bg and 0xFF
        val out = ByteArray(argb.size * 2)
        var o = 0
        for (px in argb) {
            val a = (px ushr 24) and 0xFF
            val r = (px ushr 16) and 0xFF
            val g = (px ushr 8) and 0xFF
            val b = px and 0xFF
            val cr = (r * a + bgR * (255 - a)) / 255
            val cg = (g * a + bgG * (255 - a)) / 255
            val cb = (b * a + bgB * (255 - a)) / 255
            val v = ((cr and 0xF8) shl 8) or ((cg and 0xFC) shl 3) or (cb shr 3)
            out[o++] = (v and 0xFF).toByte()          // low byte first (LE)
            out[o++] = ((v ushr 8) and 0xFF).toByte()
        }
        return out
    }
}
