package ee.zeppl.companion.ble

import android.Manifest
import android.app.Application
import android.content.Context
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows

/**
 * Drives the real PackageManager via Robolectric and toggles each of
 * the three runtime permissions in turn so the all-or-nothing semantics
 * of [BleAccess.allGranted] are pinned. A regression that allows a
 * partial grant through would crash later on the first BLE call.
 */
@RunWith(RobolectricTestRunner::class)
class BleAccessTest {

    private val context: Context = ApplicationProvider.getApplicationContext()
    private val shadowApp = Shadows.shadowOf(context as Application)

    @Test fun `allGranted false when nothing granted`() {
        // Robolectric starts with all runtime perms denied. Sanity check.
        assertFalse(BleAccess.allGranted(context))
    }

    @Test fun `allGranted true after the four required perms are granted`() {
        shadowApp.grantPermissions(
            Manifest.permission.BLUETOOTH_SCAN,
            Manifest.permission.BLUETOOTH_CONNECT,
            Manifest.permission.POST_NOTIFICATIONS,
            Manifest.permission.ANSWER_PHONE_CALLS,
        )
        assertTrue(BleAccess.allGranted(context))
    }

    @Test fun `allGranted false if any single one is missing`() {
        // Grant three of four. Each missing-one case should evaluate
        // false — otherwise the UI would let the user try to connect
        // and crash on the first scan call.
        shadowApp.grantPermissions(
            Manifest.permission.BLUETOOTH_SCAN,
            Manifest.permission.BLUETOOTH_CONNECT,
            Manifest.permission.POST_NOTIFICATIONS,
        )
        assertFalse("missing ANSWER_PHONE_CALLS", BleAccess.allGranted(context))

        shadowApp.denyPermissions(Manifest.permission.BLUETOOTH_SCAN)
        shadowApp.grantPermissions(Manifest.permission.ANSWER_PHONE_CALLS)
        assertFalse("missing BLUETOOTH_SCAN", BleAccess.allGranted(context))

        shadowApp.grantPermissions(Manifest.permission.BLUETOOTH_SCAN)
        shadowApp.denyPermissions(Manifest.permission.BLUETOOTH_CONNECT)
        assertFalse("missing BLUETOOTH_CONNECT", BleAccess.allGranted(context))

        shadowApp.grantPermissions(Manifest.permission.BLUETOOTH_CONNECT)
        shadowApp.denyPermissions(Manifest.permission.POST_NOTIFICATIONS)
        assertFalse("missing POST_NOTIFICATIONS", BleAccess.allGranted(context))
    }

    @Test fun `REQUIRED contains the exact four permissions we declare`() {
        // Lock the contract: a future change that silently adds a perm
        // here would surprise the UI flow (the request dialog wouldn't
        // match what the manifest claims).
        val expected = setOf(
            Manifest.permission.BLUETOOTH_SCAN,
            Manifest.permission.BLUETOOTH_CONNECT,
            Manifest.permission.POST_NOTIFICATIONS,
            Manifest.permission.ANSWER_PHONE_CALLS,
        )
        assertTrue(BleAccess.REQUIRED.toSet() == expected)
    }
}
