package ee.zeppl.companion.ui

import android.Manifest
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Button
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import ee.zeppl.companion.ble.CalibrationSession
import ee.zeppl.companion.ble.OutboundSink
import ee.zeppl.companion.ble.Protocol
import ee.zeppl.companion.ble.TelemetryState
import ee.zeppl.companion.cal.SpeedCalibrator

/**
 * Speed-calibration wizard (Brick 2). Correlates the phone's GPS speed against
 * the cluster's raw ECM count ([TelemetryState.speedRaw]), least-squares fits
 * the divisor ([SpeedCalibrator]), and writes it back over the link
 * ([Protocol.encodeConfig]).
 *
 * Sample collection lives in the foreground [ee.zeppl.companion.ble.BleService]
 * via [CalibrationSession], NOT in this Composable - so it keeps running while
 * the ride screen is off. This card just starts/stops the run and shows it.
 */
@Composable
fun SpeedCalibrationCard() {
    val context = LocalContext.current
    var appliedDivisor by remember { mutableStateOf<Int?>(null) }
    // Local review step: the run is stopped, showing the result to apply/redo.
    var reviewing by remember { mutableStateOf(false) }

    var hasLocation by remember {
        mutableStateOf(
            ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION) ==
                PackageManager.PERMISSION_GRANTED,
        )
    }
    val permLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { granted ->
        hasLocation = granted
        if (granted) { reviewing = false; CalibrationSession.start() }
    }

    val active = CalibrationSession.active
    val result = CalibrationSession.result

    SectionCard(title = "Speed calibration") {
        Text(
            "Ride at a steady speed and correlate phone GPS with the raw ECM count to " +
                "solve the exact speed divisor, then write it back to the cluster. " +
                "Vary your pace between roughly 15 and 55 mph for the best fit.",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )

        appliedDivisor?.let { InfoRow("Active divisor", it.toString()) }

        when {
            reviewing && result != null -> {
                InfoRow("Solved divisor", result.divisor.toString())
                InfoRow("Fit error", "%.1f mph".format(result.rmsErrorMph))
                InfoRow("From samples", result.sampleCount.toString())
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    OutlinedButton(
                        onClick = { reviewing = false; CalibrationSession.reset() },
                        modifier = Modifier.weight(1f),
                    ) { Text("Redo") }
                    Button(
                        onClick = {
                            OutboundSink.send(Protocol.encodeConfig(result.divisor))
                            appliedDivisor = result.divisor
                            reviewing = false
                            CalibrationSession.reset()
                        },
                        modifier = Modifier.weight(1f),
                    ) { Text("Apply to cluster") }
                }
            }

            active -> {
                Text(
                    "Collecting - keep riding. This continues in the background with the " +
                        "screen off; you don't need to keep this open.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                InfoRow("GPS speed", CalibrationSession.gpsMph?.let { "%.1f mph".format(it) } ?: "acquiring…")
                InfoRow("Raw count", TelemetryState.speedRaw?.toString() ?: "—")
                InfoRow("Samples", "${CalibrationSession.samples.size} (need ${SpeedCalibrator.MIN_SAMPLES})")
                result?.let {
                    InfoRow("Solved divisor", it.divisor.toString())
                    InfoRow("Fit error", "%.1f mph".format(it.rmsErrorMph))
                }
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    OutlinedButton(
                        onClick = { CalibrationSession.reset() },
                        modifier = Modifier.weight(1f),
                    ) { Text("Cancel") }
                    Button(
                        onClick = { reviewing = true; CalibrationSession.stop() },
                        enabled = result != null,
                        modifier = Modifier.weight(1f),
                    ) { Text("Finish") }
                }
            }

            else -> {
                FilledTonalButton(
                    onClick = {
                        reviewing = false
                        if (hasLocation) {
                            CalibrationSession.start()
                        } else {
                            permLauncher.launch(Manifest.permission.ACCESS_FINE_LOCATION)
                        }
                    },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Start calibration") }
            }
        }
    }
}
