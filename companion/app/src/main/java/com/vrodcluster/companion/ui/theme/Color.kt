package com.vrodcluster.companion.ui.theme

import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.ui.graphics.Color

// Brand palette mirrors the cluster's own (firmware main/display/theme.h) so
// the phone and the dash read as one product: V-Rod orange on near-black,
// with the same signal-green / beam-blue / amber / red status hues. The
// ColorScheme roles below are tuned from that seed; StatusColors carries the
// lamp/link accents that don't belong to a Material role.

private val VRodOrange       = Color(0xFFFF6600)  // primary accent (cursor, fuel fill)
private val VRodOrangeSoft   = Color(0xFFFFB68E)  // lifted tone for on-dark legibility

val VRodDarkColors = darkColorScheme(
    primary              = VRodOrange,
    onPrimary            = Color(0xFF2A1000),
    primaryContainer     = Color(0xFF7A3300),
    onPrimaryContainer   = Color(0xFFFFDBC7),
    inversePrimary       = Color(0xFF9A4500),

    secondary            = Color(0xFFE6BC7D),      // warm sand
    onSecondary          = Color(0xFF3F2E12),
    secondaryContainer   = Color(0xFF574526),
    onSecondaryContainer = Color(0xFFFFDDB0),

    tertiary             = Color(0xFF7FCBFF),      // beam blue, for informational accents
    onTertiary           = Color(0xFF00344F),
    tertiaryContainer    = Color(0xFF004C6E),
    onTertiaryContainer  = Color(0xFFCAE6FF),

    error                = Color(0xFFFF5545),
    onError              = Color(0xFF2A0400),
    errorContainer       = Color(0xFF93000A),
    onErrorContainer     = Color(0xFFFFDAD6),

    background           = Color(0xFF0E0E10),
    onBackground         = Color(0xFFE7E2E6),
    surface              = Color(0xFF0E0E10),
    onSurface            = Color(0xFFE7E2E6),
    surfaceVariant       = Color(0xFF48453F),
    onSurfaceVariant     = Color(0xFFCBC6BE),      // captions / units (echoes VROD_TEXT_DIM)

    surfaceContainerLowest  = Color(0xFF090909),
    surfaceContainerLow     = Color(0xFF161618),
    surfaceContainer        = Color(0xFF1B1B1D),
    surfaceContainerHigh    = Color(0xFF252527),
    surfaceContainerHighest = Color(0xFF303032),

    outline              = Color(0xFF938F88),
    outlineVariant       = Color(0xFF48453F),
    scrim                = Color(0xFF000000),
    inverseSurface       = Color(0xFFE7E2E6),
    inverseOnSurface     = Color(0xFF2F2F31),
)

val VRodLightColors = lightColorScheme(
    primary              = Color(0xFF9A4500),
    onPrimary            = Color(0xFFFFFFFF),
    primaryContainer     = Color(0xFFFFDBC7),
    onPrimaryContainer   = Color(0xFF331100),
    inversePrimary       = VRodOrangeSoft,

    secondary            = Color(0xFF755A2F),
    onSecondary          = Color(0xFFFFFFFF),
    secondaryContainer   = Color(0xFFFFDDB0),
    onSecondaryContainer = Color(0xFF281900),

    tertiary             = Color(0xFF00658E),
    onTertiary           = Color(0xFFFFFFFF),
    tertiaryContainer    = Color(0xFFC7E7FF),
    onTertiaryContainer  = Color(0xFF001E2E),

    error                = Color(0xFFBA1A1A),
    onError              = Color(0xFFFFFFFF),
    errorContainer       = Color(0xFFFFDAD6),
    onErrorContainer     = Color(0xFF410002),

    background           = Color(0xFFFFF8F5),
    onBackground         = Color(0xFF201A17),
    surface              = Color(0xFFFFF8F5),
    onSurface            = Color(0xFF201A17),
    surfaceVariant       = Color(0xFFF3DFD1),
    onSurfaceVariant     = Color(0xFF52443A),
    outline              = Color(0xFF847469),
    outlineVariant       = Color(0xFFD7C3B5),
)

/**
 * Status accents that are not Material roles: the connection-link and lamp
 * hues, matched to the cluster's own indicator colors so a green "linked"
 * chip on the phone is the same green as the low-beam lamp on the dash.
 */
object StatusColors {
    val Linked  = Color(0xFF33CC22)  // VROD_GREEN_SIGNAL: connected / healthy
    val Warning = Color(0xFFFFAA00)  // VROD_AMBER_WARNING: connecting / reconnecting
    val Alert   = Color(0xFFFF2200)  // VROD_RED_WARNING: fault / offline
    val Info    = Color(0xFF2299FF)  // VROD_BLUE_HIGH_BEAM: informational
}
