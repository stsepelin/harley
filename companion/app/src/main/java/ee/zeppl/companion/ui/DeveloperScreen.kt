package ee.zeppl.companion.ui

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import ee.zeppl.companion.ble.BleAccess
import ee.zeppl.companion.ble.BleConnState
import ee.zeppl.companion.ble.BleState
import ee.zeppl.companion.ble.BondedClusters
import ee.zeppl.companion.ble.ClusterNames
import ee.zeppl.companion.ble.TelemetryState

/**
 * Gated developer tools, unlocked by tapping a cluster's Firmware row seven
 * times. Calibration is divided per paired cluster - the speed-calibration
 * wizard targets whichever cluster is connected; the rest prompt to connect.
 * A raw-telemetry readout for debugging the decode sits below.
 */
@SuppressLint("MissingPermission")
@Composable
fun DeveloperScreen(onExit: () -> Unit) {
    val context = LocalContext.current
    val owner = LocalLifecycleOwner.current

    // list() does reflection + logging, and this screen recomposes at the
    // telemetry rate - so cache the bonds and refresh only on resume / link change.
    var bonded by remember {
        mutableStateOf<List<BluetoothDevice>>(
            if (BleAccess.allGranted(context)) BondedClusters.list(context) else emptyList()
        )
    }
    DisposableEffect(owner) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_RESUME && BleAccess.allGranted(context)) {
                bonded = BondedClusters.list(context)
            }
        }
        owner.lifecycle.addObserver(observer)
        onDispose { owner.lifecycle.removeObserver(observer) }
    }
    LaunchedEffect(BleState.conn) {
        if (BleAccess.allGranted(context)) bonded = BondedClusters.list(context)
    }

    val connectedName = if (BleState.conn == BleConnState.CONNECTED) BleState.deviceName else null

    ScreenColumn(title = "Developer", subtitle = "Calibration, config, and raw telemetry") {
        if (bonded.isEmpty()) {
            SectionCard(title = "Calibration") {
                Text(
                    "Pair a cluster to calibrate it.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        } else {
            bonded.forEach { dev ->
                val label = ClusterNames.display(context, dev.address, dev.name)
                val isConnected = connectedName != null &&
                    (connectedName == dev.name || connectedName == dev.address)
                Text(
                    label.uppercase(),
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.primary,
                )
                if (isConnected) {
                    SpeedCalibrationCard()
                } else {
                    SectionCard(title = "Speed calibration") {
                        Text(
                            "Connect $label to calibrate its speed divisor.",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }
        }

        SectionCard(title = "Live telemetry (raw)") {
            InfoRow("speed_raw", TelemetryState.speedRaw?.toString() ?: DASH)
            InfoRow("speed_mph", TelemetryState.speedMph?.toString() ?: DASH)
            InfoRow("rpm", TelemetryState.rpm?.toString() ?: DASH)
            InfoRow("gear", TelemetryState.gear?.toString() ?: DASH)
            InfoRow("engine_temp_c", TelemetryState.engineTempC?.toString() ?: DASH)
            InfoRow("fuel_level", TelemetryState.fuelLevel?.toString() ?: DASH)
            InfoRow("lamps", TelemetryState.lamps?.let { "0x%03X".format(it) } ?: DASH)
            InfoRow("odometer_m", TelemetryState.odometerM?.toString() ?: DASH)
            InfoRow("trip1_fuel", TelemetryState.trip1FuelTicks?.toString() ?: DASH)
            InfoRow("last_frame_ms", TelemetryState.lastFrameMs?.toString() ?: DASH)
        }

        OutlinedButton(onClick = onExit, modifier = Modifier.fillMaxWidth()) {
            Text("Disable developer mode")
        }
    }
}

private const val DASH = "—"
