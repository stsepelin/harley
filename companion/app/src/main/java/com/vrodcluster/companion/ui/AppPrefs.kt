package com.vrodcluster.companion.ui

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/**
 * In-memory UI preferences the Settings screen toggles and the theme reads.
 * Kept simple (process-lifetime) for now; a DataStore-backed store lands
 * with cluster config persistence (Brick 3), which is where user prefs will
 * live alongside the write-back settings.
 */
object AppPrefs {
    /** Use Material You wallpaper colors instead of the branded V-Rod palette. */
    var dynamicColor: Boolean by mutableStateOf(false)
}
