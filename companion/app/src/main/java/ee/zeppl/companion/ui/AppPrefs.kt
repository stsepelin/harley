package ee.zeppl.companion.ui

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
    /** Use Material You wallpaper colors instead of the branded Zeppl palette. */
    var dynamicColor: Boolean by mutableStateOf(false)

    /** Unlocked by tapping the Firmware row (Cluster page) seven times. Adds a
     *  Developer tab with calibration / cluster-config tools. */
    var developerMode: Boolean by mutableStateOf(false)
}
