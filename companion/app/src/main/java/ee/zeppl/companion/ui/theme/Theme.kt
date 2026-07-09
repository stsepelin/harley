package ee.zeppl.companion.ui.theme

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.MaterialExpressiveTheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext

/**
 * App theme. Brand-first by default (`dynamicColor = false`) so the Zeppl
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
fun ZepplTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    dynamicColor: Boolean = false,
    content: @Composable () -> Unit,
) {
    val context = LocalContext.current
    val colorScheme = when {
        // "Match device colors": keep the wallpaper-derived accents but force a
        // clean light scheme with white surfaces - the raw dynamic neutrals came
        // out muddy/tinted, so we override the backgrounds to white.
        dynamicColor && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S ->
            dynamicLightColorScheme(context).copy(
                background = Color.White,
                surface = Color.White,
                surfaceContainerLowest = Color.White,
                surfaceContainerLow = Color(0xFFF7F7F7),
                surfaceContainer = Color(0xFFF2F2F2),
                surfaceContainerHigh = Color(0xFFECECEC),
                surfaceContainerHighest = Color(0xFFE6E6E6),
                onBackground = Color(0xFF1B1B1B),
                onSurface = Color(0xFF1B1B1B),
            )
        darkTheme -> ZepplDarkColors
        else      -> ZepplLightColors
    }
    MaterialExpressiveTheme(
        colorScheme = colorScheme,
        typography  = ZepplTypography,
        content     = content,
    )
}
