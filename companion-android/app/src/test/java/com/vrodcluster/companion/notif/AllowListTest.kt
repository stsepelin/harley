package com.vrodcluster.companion.notif

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/**
 * Default-allow / blocklist semantics. Drives the real SharedPreferences
 * backed by Robolectric, so a regression in storage format (e.g. a
 * future schema bump) would surface immediately.
 */
@RunWith(RobolectricTestRunner::class)
class AllowListTest {

    private val context: Context = ApplicationProvider.getApplicationContext()

    @After fun clearPrefs() {
        context.getSharedPreferences("vrod.allowlist", Context.MODE_PRIVATE)
               .edit().clear().commit()
    }

    // --- default policy ---------------------------------------------------

    @Test fun `default policy allows every package`() {
        assertTrue(AllowList.isAllowed(context, "com.whatsapp"))
        assertTrue(AllowList.isAllowed(context, "org.telegram.messenger"))
        assertTrue(AllowList.isAllowed(context, "anything.really"))
    }

    @Test fun `default blocked set is empty`() {
        assertEquals(emptySet<String>(), AllowList.blocked(context))
    }

    // --- mute / unmute ----------------------------------------------------

    @Test fun `set false mutes a package`() {
        AllowList.set(context, "com.spotify.music", false)
        assertFalse(AllowList.isAllowed(context, "com.spotify.music"))
        assertEquals(setOf("com.spotify.music"), AllowList.blocked(context))
    }

    @Test fun `set true unmutes a previously blocked package`() {
        AllowList.set(context, "com.spotify.music", false)
        AllowList.set(context, "com.spotify.music", true)
        assertTrue(AllowList.isAllowed(context, "com.spotify.music"))
        assertEquals(emptySet<String>(), AllowList.blocked(context))
    }

    @Test fun `set true on already-allowed package is a no-op`() {
        AllowList.set(context, "com.foo", true)
        assertTrue(AllowList.isAllowed(context, "com.foo"))
        assertEquals(emptySet<String>(), AllowList.blocked(context))
    }

    @Test fun `set false twice keeps the package muted only once`() {
        AllowList.set(context, "com.foo", false)
        AllowList.set(context, "com.foo", false)
        assertEquals(setOf("com.foo"), AllowList.blocked(context))
    }

    @Test fun `multiple mutes accumulate independently`() {
        AllowList.set(context, "a", false)
        AllowList.set(context, "b", false)
        AllowList.set(context, "c", false)
        assertEquals(setOf("a", "b", "c"), AllowList.blocked(context))
        assertFalse(AllowList.isAllowed(context, "a"))
        assertFalse(AllowList.isAllowed(context, "b"))
        assertFalse(AllowList.isAllowed(context, "c"))
        // Anything else remains allowed by default.
        assertTrue (AllowList.isAllowed(context, "d"))
    }

    // --- migration --------------------------------------------------------

    @Test fun `v1 packages key does not affect default-allow semantics`() {
        // Simulate an old install that wrote to the legacy "packages"
        // key. The current implementation reads "blocked_v2" only, so
        // the stale data is silently ignored — no surprise mutes.
        context.getSharedPreferences("vrod.allowlist", Context.MODE_PRIVATE)
               .edit().putStringSet("packages", setOf("com.legacy")).commit()
        assertTrue(AllowList.isAllowed(context, "com.legacy"))
        assertTrue(AllowList.isAllowed(context, "com.anything"))
        assertEquals(emptySet<String>(), AllowList.blocked(context))
    }
}
