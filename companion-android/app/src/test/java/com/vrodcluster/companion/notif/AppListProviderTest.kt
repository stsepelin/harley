package com.vrodcluster.companion.notif

import android.content.Context
import android.content.pm.ApplicationInfo
import android.content.pm.PackageInfo
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows
import org.robolectric.shadows.ShadowPackageManager

/**
 * Drives the real PackageManager (via Robolectric's shadow) so the
 * system-app filter, sort order, and own-package exclusion are tested
 * end-to-end without an emulator.
 */
@RunWith(RobolectricTestRunner::class)
class AppListProviderTest {

    private val context: Context = ApplicationProvider.getApplicationContext()
    private lateinit var shadowPm: ShadowPackageManager

    @Before fun setup() {
        shadowPm = Shadows.shadowOf(context.packageManager)
    }

    private fun install(pkg: String, label: String, flags: Int = 0) {
        val pi = PackageInfo().apply {
            packageName = pkg
            applicationInfo = ApplicationInfo().apply {
                this.packageName       = pkg
                this.flags             = flags
                // nonLocalizedLabel short-circuits the resource lookup in
                // loadLabel(); without it Robolectric would try to read
                // labelRes which we'd have to wire to a real string res.
                this.nonLocalizedLabel = label
            }
        }
        shadowPm.installPackage(pi)
    }

    private fun labels(): List<String> = AppListProvider.list(context).map { it.label }

    // --- happy path -------------------------------------------------------

    @Test fun `lists user-installed apps sorted alphabetically by label`() {
        install("com.zeta",  "Zeta")
        install("com.alpha", "Alpha")
        install("com.beta",  "Beta")
        assertEquals(listOf("Alpha", "Beta", "Zeta"), labels())
    }

    @Test fun `sort is case-insensitive`() {
        install("com.up",  "Bravo")
        install("com.dn",  "alpha")
        assertEquals(listOf("alpha", "Bravo"), labels())
    }

    // --- filtering --------------------------------------------------------

    @Test fun `excludes pure system apps`() {
        install("com.systemapp", "System", flags = ApplicationInfo.FLAG_SYSTEM)
        install("com.userapp",   "User")
        val ls = labels()
        assertFalse("system app should be excluded", "System" in ls)
        assertTrue ("user app should be included",   "User"   in ls)
    }

    @Test fun `includes updated system apps`() {
        // Gmail / Messages / Phone are pre-installed but get OTA updates,
        // so they carry both flags. We want them shown.
        install(
            "com.gmail", "Gmail",
            flags = ApplicationInfo.FLAG_SYSTEM or ApplicationInfo.FLAG_UPDATED_SYSTEM_APP,
        )
        assertTrue("Gmail" in labels())
    }

    @Test fun `excludes own package`() {
        install(context.packageName, "Self")
        install("com.other",         "Other")
        val ls = labels()
        assertFalse("Self" in ls)
        assertTrue ("Other" in ls)
    }

    @Test fun `entries without a label are dropped`() {
        install("com.noname", "")
        install("com.named",  "Named")
        assertEquals(listOf("Named"), labels())
    }

    // --- shape ------------------------------------------------------------

    @Test fun `each entry carries a non-null icon`() {
        install("com.foo", "Foo")
        val foo = AppListProvider.list(context).first { it.packageName == "com.foo" }
        assertEquals("Foo", foo.label)
        assertNotNull(foo.icon)
    }
}
