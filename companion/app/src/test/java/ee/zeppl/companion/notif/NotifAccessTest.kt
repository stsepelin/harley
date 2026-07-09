package ee.zeppl.companion.notif

import android.content.Context
import android.content.Intent
import android.provider.Settings
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/**
 * Settings.Secure parsing for notification-access detection. Colon-
 * delimited component list with substring-match traps — explicitly
 * tested here so a refactor to e.g. `contains()` would fail the suite.
 */
@RunWith(RobolectricTestRunner::class)
class NotifAccessTest {

    private val context: Context = ApplicationProvider.getApplicationContext()

    private val ourComponent =
        "ee.zeppl.companion/ee.zeppl.companion.notif.ZepplNotifListener"

    private fun setEnabled(value: String?) {
        Settings.Secure.putString(
            context.contentResolver,
            "enabled_notification_listeners",
            value,
        )
    }

    @Test fun `isGranted returns false when setting absent`() {
        setEnabled(null)
        assertFalse(NotifAccess.isGranted(context))
    }

    @Test fun `isGranted returns false when empty`() {
        setEnabled("")
        assertFalse(NotifAccess.isGranted(context))
    }

    @Test fun `isGranted returns false when only other listeners enabled`() {
        setEnabled("com.other/com.other.Listener:com.another/com.another.Listener")
        assertFalse(NotifAccess.isGranted(context))
    }

    @Test fun `isGranted returns true when our listener is in the list`() {
        setEnabled("com.other/com.other.Listener:$ourComponent:com.last/com.last.Listener")
        assertTrue(NotifAccess.isGranted(context))
    }

    @Test fun `isGranted returns true when only our listener enabled`() {
        setEnabled(ourComponent)
        assertTrue(NotifAccess.isGranted(context))
    }

    @Test fun `partial substring match does not falsely grant`() {
        // A neighbour package with a name that has ours as a prefix
        // must not be mistaken for us; we split on `:` and compare
        // entries exactly.
        setEnabled("ee.zeppl.companion.evil/ee.zeppl.companion.evil.Listener")
        assertFalse(NotifAccess.isGranted(context))
    }

    @Test fun `grantIntent points at the notification settings page`() {
        val intent = NotifAccess.grantIntent()
        assertEquals(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS, intent.action)
        assertTrue(intent.flags and Intent.FLAG_ACTIVITY_NEW_TASK != 0)
    }
}
