package ee.zeppl.companion.ble

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class IconCodecTest {

    @Test fun `icon id is stable and never zero`() {
        assertEquals(IconCodec.iconId("com.whatsapp"), IconCodec.iconId("com.whatsapp"))
        assertNotEquals(IconCodec.iconId("com.whatsapp"), IconCodec.iconId("org.telegram.messenger"))
        // a package whose hashCode is 0 would still map to non-zero
        assertTrue(IconCodec.iconId("") != 0u)
    }

    @Test fun `opaque pixel packs to little-endian rgb565`() {
        // Fully-opaque pure red -> R=31,G=0,B=0 -> 0xF800 -> LE bytes 00 F8.
        val out = IconCodec.argbToRgb565Opaque(intArrayOf(0xFFFF0000.toInt()))
        assertArrayEquals(byteArrayOf(0x00, 0xF8.toByte()), out)
    }

    @Test fun `fully transparent pixel becomes the background`() {
        // Alpha 0 over the default banner bg 0x1A1A1A.
        val out = IconCodec.argbToRgb565Opaque(intArrayOf(0x00FFFFFF))
        // 0x1A -> R5=3 (0x18>>3), G6=6 (0x18>>2), B5=3 => (3<<11)|(6<<5)|3 = 0x18C3
        assertArrayEquals(byteArrayOf(0xC3.toByte(), 0x18), out)
    }

    @Test fun `output is two bytes per pixel`() {
        val out = IconCodec.argbToRgb565Opaque(IntArray(48 * 48))
        assertEquals(48 * 48 * 2, out.size)
    }
}
