package ee.zeppl.companion.notif

import ee.zeppl.companion.ble.Protocol
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

    private val OWN = "ee.zeppl.companion"

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

    @Test fun `CallStyle template maps to CALL even with null category`() {
        // Modern dialers (API 31+) use Notification.CallStyle and don't
        // always set category="call" alongside it. Template wins so the
        // call still classifies correctly.
        assertEquals(
            Protocol.NotifKind.CALL,
            NotifMapper.kindFor(null, "android.app.Notification\$CallStyle"),
        )
    }

    @Test fun `CallStyle template wins over generic category`() {
        // A CallStyle notification with a non-call category (some dialers
        // tag it "service" or similar) should still classify as CALL.
        assertEquals(
            Protocol.NotifKind.CALL,
            NotifMapper.kindFor("service", "android.app.Notification\$CallStyle"),
        )
    }

    // --- drop rules --------------------------------------------------------

    @Test fun `own package is dropped`() {
        assertNull(encode(packageName = OWN))
    }

    @Test fun `ongoing notifications are dropped`() {
        assertNull(encode(isOngoing = true))
    }

    @Test fun `ongoing call notification is kept`() {
        // Phone calls post an ongoing notification that persists for the
        // duration of the call. The "ongoing" filter was originally
        // intended to suppress long-running app state (Spotify, Maps),
        // but it was also silently swallowing incoming calls — the one
        // notification we most want to surface on the cluster.
        val out = encode(category = "call", isOngoing = true)
        assertEquals(0x01.toByte(), out!![0])
    }

    @Test fun `foreground service notifications are dropped`() {
        // FLAG_FOREGROUND_SERVICE = 0x40
        assertNull(encode(flags = 0x40))
    }

    @Test fun `foreground service call notification is kept`() {
        // Some dialers run as a foreground service for the duration of a
        // call. Same reasoning as the ongoing exemption above.
        val out = encode(category = "call", flags = 0x40)
        assertEquals(0x01.toByte(), out!![0])
    }

    @Test fun `CallStyle template incoming call survives ongoing and fg-service filters`() {
        // Reproduces the Samsung Galaxy Fold incoming-call case: dialer
        // posts a CallStyle notification with category null, isOngoing
        // true, and FLAG_FOREGROUND_SERVICE set. Pre-fix this combination
        // was silently dropped; the rider missed every incoming call.
        val out = encode(
            category    = null,
            template    = "android.app.Notification\$CallStyle",
            isOngoing   = true,
            flags       = 0x40,   // FLAG_FOREGROUND_SERVICE
            title       = "John Smith",
            text        = "Incoming call",
        )
        assertEquals(0x01.toByte(), out!![0])    // PHONE_EVT_NOTIF
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
