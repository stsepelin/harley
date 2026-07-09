package com.vrodcluster.companion.ui

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.History
import androidx.compose.material.icons.filled.Speed
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material.icons.filled.TwoWheeler
import androidx.compose.ui.graphics.vector.ImageVector
import kotlinx.serialization.Serializable

// Type-safe navigation routes (Navigation Compose 2.8+). Each destination is
// a @Serializable object so navigate(Ride) and composable<Ride> {} are
// checked at compile time - no string routes to drift out of sync.

@Serializable object Ride       // live telemetry dashboard (home)
@Serializable object Cluster    // device hub: connection, bonds, calibration, diagnostics
@Serializable object Settings   // notification/media relay + config + appearance
@Serializable object History    // trips + economy trends

@Serializable object Scan       // pushed from Cluster: pick a cluster to connect
@Serializable object AppList    // pushed from Settings: choose forwarding apps

/** A top-level tab in the navigation suite. `route` is one of the objects above. */
data class TopLevelDestination(
    val route: Any,
    val label: String,
    val icon: ImageVector,
)

val TOP_LEVEL_DESTINATIONS = listOf(
    TopLevelDestination(Ride,     "Ride",     Icons.Filled.Speed),
    TopLevelDestination(Cluster,  "Cluster",  Icons.Filled.TwoWheeler),
    TopLevelDestination(Settings, "Settings", Icons.Filled.Tune),
    TopLevelDestination(History,  "History",  Icons.Filled.History),
)
