package com.vrodcluster.companion.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.runtime.saveable.rememberSaveable

private enum class Screen { Status, AppList }

/**
 * Two-screen root. A navigation library is overkill for the
 * configuration→status loop; `rememberSaveable` survives config
 * changes (rotation, dark-mode toggle) without a NavController.
 */
@Composable
fun App() {
    var screen by rememberSaveable { mutableStateOf(Screen.Status) }
    MaterialTheme {
        when (screen) {
            Screen.Status  -> StatusScreen (onConfigureApps = { screen = Screen.AppList })
            Screen.AppList -> AppListScreen(onBack          = { screen = Screen.Status })
        }
    }
}
