package com.vrodcluster.companion.notif

import com.vrodcluster.companion.ble.Protocol
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * Mapping rules between Android notifications and the cluster wire
 * format. Drives [NotifMapper] with primitive inputs so we don't need
 * the Android framework at test time.
 */
class NotifMapperTest {

    private val OWN = "com.vrodcluster.companion"

    private fun encode(
        ownPackage:  String  = OWN,
        packageName: String  = "com.example.app",
        isOngoing:   Boolean = false,
        flags:       Int     = 0,
        key:         String  = "0|com.example.app|0|tag|0",
        category:    String? = null,
        template:    String? = null,
        title:       String  = "Title",
        text:        String  = "Body",
    ): ByteArray? = NotifMapper.encodePost(
        ownPackage, packageName, isOngoing, flags, key, category, template, title, text,
    )

    // --- classification ----------------------------------------------------

    @Test fun `category call maps to CALL`() {
        assertEquals(Protocol.NotifKind.CALL, NotifMapper.kindFor("call"))
    }

    @Test fun `category missed_call maps to CALL`() {
        assertEquals(Protocol.NotifKind.CALL, NotifMapper.kindFor("missed_call"))
    }

    @Test fun `category msg maps to SMS`() {
        assertEquals(Protocol.NotifKind.SMS, NotifMapper.kindFor("msg"))
    }

    @Test fun `unknown category falls back to APP`() {
        assertEquals(Protocol.NotifKind.APP, NotifMapper.kindFor(null))
        assertEquals(Protocol.NotifKind.APP, NotifMapper.kindFor("email"))
        assertEquals(Protocol.NotifKind.APP, NotifMapper.kindFor("promo"))
    }

    // --- drop rules --------------------------------------------------------

    @Test fun `own package is dropped`() {
        assertNull(encode(packageName = OWN))
    }

    @Test fun `ongoing notifications are dropped`() {
        assertNull(encode(isOngoing = true))
    }

    @Test fun `foreground service notifications are dropped`() {
        // FLAG_FOREGROUND_SERVICE = 0x40
        assertNull(encode(flags = 0x40))
    }

    @Test fun `group summary notifications are dropped`() {
        // FLAG_GROUP_SUMMARY = 0x200. Messaging apps (Telegram, Gmail)
        // post a summary alongside each child; we want the child only.
        assertNull(encode(flags = 0x200))
    }

    @Test fun `transport-category notifications are dropped`() {
        // Spotify / YouTube Music post their now-playing notification
        // with CATEGORY_TRANSPORT. MediaWatcher already pushes that
        // data via the proper MEDIA channel.
        assertNull(encode(category = "transport"))
    }

    @Test fun `MediaStyle template notifications are dropped`() {
        // Same idea as transport, but matched via the template extra
        // since not every media app sets CATEGORY_TRANSPORT.
        assertNull(encode(template = "android.app.Notification\$MediaStyle"))
    }

    @Test fun `non-media template does not interfere with normal flow`() {
        // BigTextStyle and similar should not get dropped.
        val out = encode(template = "android.app.Notification\$BigTextStyle")
        assertEquals(0x01.toByte(), out!![0])
    }

    @Test fun `ordinary notification is encoded`() {
        val out = encode(category = "msg", title = "Alice", text = "hey")
        assertEquals(0x01.toByte(), out!![0])    // PHONE_EVT_NOTIF
    }

    // --- stableId ----------------------------------------------------------

    @Test fun `stableId is consistent for same key`() {
        val k = "0|com.foo|123|tag|0"
        assertEquals(NotifMapper.stableId(k), NotifMapper.stableId(k))
    }

    @Test fun `stableId differs for different keys`() {
        val a = NotifMapper.stableId("0|com.foo|1|tag|0")
        val b = NotifMapper.stableId("0|com.foo|2|tag|0")
        assertNotEquals(a, b)
    }

    // --- end-to-end byte shape --------------------------------------------

    @Test fun `encoded post matches Protocol_encodeNotif directly`() {
        val key   = "0|com.example|7|null|0"
        val out   = encode(category = "msg", key = key, title = "Bob", text = "ping")
        val want  = Protocol.encodeNotif(
            NotifMapper.stableId(key), Protocol.NotifKind.SMS, "Bob", "ping"
        )
        assertArrayEquals(want, out)
    }
}
