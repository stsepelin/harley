package com.vrodcluster.companion.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.BluetoothDisabled
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.vrodcluster.companion.ble.BleConnState
import com.vrodcluster.companion.ble.BleState
import com.vrodcluster.companion.ble.TelemetryState
import com.vrodcluster.companion.ui.theme.VRodType

/**
 * The Ride dashboard - the home screen and the hero surface. Big glanceable
 * speed, a rpm/gear/temp strip, and odometer/trip cards. Renders live from
 * [TelemetryState] once frames arrive (Brick 1); shows an offline empty-state
 * before any link, and retains last-known values when a live link drops.
 */
@Composable
fun RideScreen(onSetUpLink: () -> Unit) {
    val hasData = TelemetryState.lastFrameMs != null
    val connected = BleState.conn == BleConnState.CONNECTED

    if (!hasData && !connected) {
        EmptyState(
            icon = Icons.Filled.BluetoothDisabled,
            title = "No live data yet",
            body = "Connect your cluster to stream live speed, rpm, gear, and trip data here.",
            action = { Button(onClick = onSetUpLink) { Text("Set up link") } },
        )
        return
    }

    ScreenColumn(
        title = "Ride",
        subtitle = if (connected) "Live from cluster" else "Offline — showing last known",
    ) {
        HeroSpeed(TelemetryState.speedMph)

        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            MetricCard("RPM", TelemetryState.rpm?.toString() ?: DASH, Modifier.weight(1f))
            MetricCard("GEAR", gearLabel(TelemetryState.gear), Modifier.weight(1f))
            MetricCard("ENGINE", tempLabel(TelemetryState.engineTempC), Modifier.weight(1f))
        }

        SectionCard(title = "Odometer") {
            InfoRow("Total", milesLabel(TelemetryState.odometerM, decimals = 0))
        }

        SectionCard(title = "Trips") {
            InfoRow("Trip 1", milesLabel(TelemetryState.trip1M, decimals = 1))
            InfoRow("Trip 2", milesLabel(TelemetryState.trip2M, decimals = 1))
        }
    }
}

@Composable
private fun HeroSpeed(mph: Int?) {
    Box(Modifier.fillMaxWidth().padding(vertical = 8.dp), contentAlignment = Alignment.Center) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(
                mph?.toString() ?: DASH,
                style = VRodType.heroDigits,
                color = MaterialTheme.colorScheme.primary,
            )
            Text(
                "MPH",
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                fontWeight = FontWeight.SemiBold,
            )
        }
    }
}

@Composable
private fun MetricCard(label: String, value: String, modifier: Modifier = Modifier) {
    SectionCard(modifier = modifier) {
        Text(
            label,
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Text(value, style = VRodType.statDigits, color = MaterialTheme.colorScheme.onSurface)
    }
}

private const val DASH = "—"

private fun gearLabel(gear: Int?): String = when (gear) {
    null -> DASH
    0    -> "N"
    in 1..6 -> gear.toString()
    else -> DASH
}

private fun tempLabel(c: Int?): String = if (c == null) DASH else "$c°"

private fun milesLabel(meters: Long?, decimals: Int): String {
    if (meters == null) return DASH
    val miles = meters / 1609.344
    return "%,.${decimals}f mi".format(miles)
}
