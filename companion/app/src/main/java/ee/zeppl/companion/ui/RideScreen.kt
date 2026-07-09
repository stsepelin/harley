package ee.zeppl.companion.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import android.annotation.SuppressLint
import android.content.Context
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.BluetoothDisabled
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import ee.zeppl.companion.ble.BleAccess
import ee.zeppl.companion.ble.BleConnState
import ee.zeppl.companion.ble.BleService
import ee.zeppl.companion.ble.BleState
import ee.zeppl.companion.ble.BondedClusters
import ee.zeppl.companion.ble.TelemetryState
import ee.zeppl.companion.ui.theme.ZepplType

/**
 * The Ride dashboard - home screen and hero surface. A big glanceable speed,
 * a rpm/gear/temp strip, and odometer/trip cards, stacked in one column so the
 * speed stays the star. Live from [TelemetryState] (Brick 1); offline
 * empty-state before any link; retains last-known values when a link drops.
 * The shared ScreenColumn caps the width so the stack stays a tidy centered
 * column on the unfolded Fold 6 inner screen rather than stretching.
 */
@Composable
fun RideScreen(onSetUpLink: () -> Unit) {
    val context = LocalContext.current
    val owner = LocalLifecycleOwner.current
    // Bonds can change while backgrounded; refresh on resume so the offline
    // action stays in sync with the Cluster page.
    var bondedAddress by remember { mutableStateOf(bondedClusterAddress(context)) }
    DisposableEffect(owner) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_RESUME) bondedAddress = bondedClusterAddress(context)
        }
        owner.lifecycle.addObserver(observer)
        onDispose { owner.lifecycle.removeObserver(observer) }
    }

    val connected = BleState.conn == BleConnState.CONNECTED

    // Only show live metrics while actually connected - no stale last-known
    // values lingering after a disconnect / forget / dropped link.
    if (!connected) {
        // Mirror the Cluster page's primary action: reconnect to a known cluster
        // (by address, autoConnect) if we have one, else send them to set one up.
        val addr = bondedAddress
        EmptyPage(
            pageTitle = "Ride",
            icon = Icons.Filled.BluetoothDisabled,
            title = if (addr != null) "Cluster offline" else "No live data yet",
            body = if (addr != null) {
                "Reconnect to your cluster to stream live speed, rpm, gear, and trip data."
            } else {
                "Connect your cluster to stream live speed, rpm, gear, and trip data here."
            },
            action = {
                if (addr != null) {
                    Button(onClick = {
                        BleService.start(context, address = addr, autoConnect = true)
                    }) { Text("Reconnect") }
                } else {
                    Button(onClick = onSetUpLink) { Text("Set up cluster") }
                }
            },
        )
        return
    }

    ScreenColumn(title = "Ride", subtitle = "Live from cluster") {
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

        FuelEconomyCard()
    }
}

@Composable
private fun HeroSpeed(mph: Int?) {
    Box(Modifier.fillMaxWidth().padding(vertical = 8.dp), contentAlignment = Alignment.Center) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(
                mph?.toString() ?: DASH,
                style = ZepplType.heroDigits,
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
            maxLines = 1,
        )
        Text(
            value,
            style = ZepplType.statDigits,
            color = MaterialTheme.colorScheme.onSurface,
            maxLines = 1,
            softWrap = false,
        )
    }
}

private const val DASH = "—"

@SuppressLint("MissingPermission")
private fun bondedClusterAddress(context: Context): String? =
    if (BleAccess.allGranted(context)) BondedClusters.list(context).firstOrNull()?.address else null

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
