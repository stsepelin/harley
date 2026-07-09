package com.vrodcluster.companion.ui.theme

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.MaterialExpressiveTheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalContext

/**
 * App theme. Brand-first by default (`dynamicColor = false`) so the V-Rod
 * identity shows on every phone rather than the user's wallpaper - a vehicle
 * brand wants its own palette. The Material You path stays available for a
 * future settings toggle.
 *
 * Wraps [MaterialExpressiveTheme], which defaults the motion scheme to the
 * expressive (spring-based) system - the single biggest lever for the app
 * feeling alive rather than flat.
 */
@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun VRodTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    dynamicColor: Boolean = false,
    content: @Composable () -> Unit,
) {
    val context = LocalContext.current
    val colorScheme = when {
        dynamicColor && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S ->
            if (darkTheme) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
        darkTheme -> VRodDarkColors
        else      -> VRodLightColors
    }
    MaterialExpressiveTheme(
        colorScheme = colorScheme,
        typography  = VRodTypography,
        content     = content,
    )
}
