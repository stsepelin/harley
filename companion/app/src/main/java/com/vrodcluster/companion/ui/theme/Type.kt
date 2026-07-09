package com.vrodcluster.companion.ui.theme

import androidx.compose.material3.Typography
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

// Body/label text uses the platform default; only the display end of the
// scale is tightened and made heavier so headline values read with the
// cluster's confident, condensed feel. Telemetry numbers use a dedicated
// monospaced style (VRodType.heroDigits) so digits are tabular and don't
// jitter width as they change - the same discipline the firmware applies
// with JetBrains Mono Bold on the dash.
val VRodTypography = Typography().run {
    copy(
        displayLarge  = displayLarge.copy(fontWeight = FontWeight.SemiBold, letterSpacing = (-0.5).sp),
        displayMedium = displayMedium.copy(fontWeight = FontWeight.SemiBold, letterSpacing = (-0.25).sp),
        headlineLarge = headlineLarge.copy(fontWeight = FontWeight.SemiBold),
        headlineMedium = headlineMedium.copy(fontWeight = FontWeight.SemiBold),
        titleLarge    = titleLarge.copy(fontWeight = FontWeight.SemiBold),
    )
}

object VRodType {
    /** Big tabular readout for live telemetry (speed, rpm, odometer). */
    val heroDigits = TextStyle(
        fontFamily = FontFamily.Monospace,
        fontWeight = FontWeight.Bold,
        fontSize   = 64.sp,
        letterSpacing = (-1).sp,
    )

    /** Secondary tabular readout for trip / economy cards. */
    val statDigits = TextStyle(
        fontFamily = FontFamily.Monospace,
        fontWeight = FontWeight.SemiBold,
        fontSize   = 24.sp,
    )
}
