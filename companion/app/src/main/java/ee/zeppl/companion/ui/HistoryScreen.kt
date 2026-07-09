package ee.zeppl.companion.ui

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Timeline
import androidx.compose.runtime.Composable

/**
 * Trip logs and fuel-economy trends. Populated once telemetry streaming +
 * fuel calibration land (Bricks 1 and 4); the History tab is where the app
 * adds value the cluster's own screen can't - persistent ride history and
 * economy over time.
 */
@Composable
fun HistoryScreen() {
    EmptyPage(
        pageTitle = "History",
        icon = Icons.Filled.Timeline,
        title = "No rides logged yet",
        body = "Once live telemetry is streaming, your rides and fuel-economy trends will collect here.",
    )
}
