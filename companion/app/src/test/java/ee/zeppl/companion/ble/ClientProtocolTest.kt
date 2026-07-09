package ee.zeppl.companion.ble

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * Cluster → phone decoder. Mirrors the firmware encoder fixtures in
 * `test_phone_protocol.c::test_encode_*`. If either side moves, the
 * matching test on the other side has to move too.
 */
class ClientProtocolTest {

    private fun pkt(type: Int, vararg payload: Int): ByteArray {
        val len = payload.size
        val out = ByteArray(3 + len)
        out[0] = type.toByte()
        out[1] = (len and 0xFF).toByte()
        out[2] = ((len shr 8) and 0xFF).toByte()
        payload.forEachIndexed { i, v -> out[3 + i] = v.toByte() }
        return out
    }

    @Test fun `decodes no-payload call accept`() {
        assertEquals(ClientCommand.CallAccept, ClientProtocol.decode(pkt(0x10)))
    }

    @Test fun `decodes media play_pause`() {
        assertEquals(ClientCommand.MediaPlayPause, ClientProtocol.decode(pkt(0x21)))
    }

    @Test fun `decodes notif dismiss with little-endian id`() {
        val cmd = ClientProtocol.decode(pkt(0x30, 0xEF, 0xBE, 0xAD, 0xDE))
        assertEquals(ClientCommand.NotifDismiss(0xDEADBEEFu), cmd)
    }

    @Test fun `unknown type returns null`() {
        // 0x99 is unassigned in the cluster's command range — drop.
        assertNull(ClientProtocol.decode(pkt(0x99)))
    }

    @Test fun `truncated header returns null`() {
        // Only one byte — no room for the LE u16 length.
        assertNull(ClientProtocol.decode(byteArrayOf(0x21)))
    }

    @Test fun `dismiss with wrong payload length returns null`() {
        // Type says DISMISS (which always carries a u32) but payload_len
        // is 2 — corrupted framing, drop rather than guess.
        val buf = byteArrayOf(0x30, 0x02, 0x00, 0xAA.toByte(), 0xBB.toByte())
        assertNull(ClientProtocol.decode(buf))
    }
}
