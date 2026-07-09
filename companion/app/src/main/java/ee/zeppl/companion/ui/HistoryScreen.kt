package ee.zeppl.companion.ui

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Timeline
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.platform.LocalContext
import ee.zeppl.companion.cal.FuelEconomy
import ee.zeppl.companion.ride.RideRecord
import ee.zeppl.companion.ride.RideStore
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Ride history + per-ride economy - the persistent log the cluster's own screen
 * can't keep. Reads [RideStore] (rides are recorded in the background by
 * [ee.zeppl.companion.ble.RideCollector]); shows the empty state until the
 * first ride lands.
 */
@Composable
fun HistoryScreen() {
    val context = LocalContext.current
    LaunchedEffect(Unit) { RideStore.ensureLoaded(context) }
    val rides = RideStore.rides

    if (rides.isEmpty()) {
        EmptyPage(
            pageTitle = "History",
            icon = Icons.Filled.Timeline,
            title = "No rides logged yet",
            body = "Ride with the cluster connected and your trips - distance, duration, and fuel economy - collect here automatically.",
        )
        return
    }

    val mlPerTick = FuelPrefs.mlPerTick(context)
    ScreenColumn(title = "History", subtitle = ridesSummary(rides)) {
        rides.forEach { RideCard(it, mlPerTick) }
    }
}

@Composable
private fun RideCard(ride: RideRecord, mlPerTick: Double) {
    SectionCard(title = rideDate(ride.startEpochMs)) {
        InfoRow("Distance", milesLabel(ride.distanceMeters))
        InfoRow("Duration", durationLabel(ride.movingMs))
        InfoRow("Avg speed", avgSpeedLabel(ride.distanceMeters, ride.movingMs))
        InfoRow("Max speed", "${ride.maxSpeedMph} mph")
        economyLabel(ride, mlPerTick)?.let { InfoRow("Economy", it) }
    }
}

private const val METERS_PER_MILE = 1609.344

private fun ridesSummary(rides: List<RideRecord>): String {
    val totalMiles = rides.sumOf { it.distanceMeters } / METERS_PER_MILE
    val n = rides.size
    return "%d ride%s, %,.0f mi total".format(n, if (n == 1) "" else "s", totalMiles)
}

private val dateFmt = SimpleDateFormat("EEE d MMM, HH:mm", Locale.getDefault())

private fun rideDate(epochMs: Long): String = dateFmt.format(Date(epochMs))

private fun milesLabel(meters: Long): String = "%,.1f mi".format(meters / METERS_PER_MILE)

private fun durationLabel(ms: Long): String {
    val totalMin = (ms / 60_000).toInt()
    val h = totalMin / 60
    val m = totalMin % 60
    return if (h > 0) "${h}h ${m}m" else "${m}m"
}

private fun avgSpeedLabel(meters: Long, movingMs: Long): String {
    if (movingMs <= 0) return "—"
    val mph = (meters / METERS_PER_MILE) / (movingMs / 3_600_000.0)
    return "%.0f mph".format(mph)
}

private fun economyLabel(ride: RideRecord, mlPerTick: Double): String? {
    if (ride.fuelTicks <= 0 || mlPerTick <= 0.0) return null
    val e = FuelEconomy.economy(ride.distanceMeters, ride.fuelTicks, mlPerTick) ?: return null
    return "%.0f mpg".format(e.mpgUs)
}
