package ee.zeppl.companion.ui

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
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
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import ee.zeppl.companion.ble.OutboundSink
import ee.zeppl.companion.ble.Protocol
import ee.zeppl.companion.ble.TelemetryState
import ee.zeppl.companion.cal.SpeedCalibrator
import kotlinx.coroutines.delay

private const val MS_TO_MPH = 2.2369362920544

private enum class CalPhase { IDLE, COLLECTING, DONE }

/**
 * Speed-calibration wizard (Brick 2). Ride at a steady speed and this
 * correlates the phone's GPS speed against the cluster's raw ECM count
 * ([TelemetryState.speedRaw]) once a second, least-squares fits the divisor
 * ([SpeedCalibrator]), and writes it back over the link
 * ([Protocol.encodeConfig]). The cluster persists it to NVS.
 *
 * The math is pure and unit-tested; everything Android-specific (GPS, runtime
 * permission) lives here.
 */
@Composable
fun SpeedCalibrationCard() {
    val context = LocalContext.current

    var phase by remember { mutableStateOf(CalPhase.IDLE) }
    var gpsMph by remember { mutableStateOf<Double?>(null) }
    val samples = remember { mutableStateListOf<SpeedCalibrator.Sample>() }
    var result by remember { mutableStateOf<SpeedCalibrator.Result?>(null) }
    var appliedDivisor by remember { mutableStateOf<Int?>(null) }

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
        if (granted) phase = CalPhase.COLLECTING
    }

    // GPS updates only flow while collecting; released the moment we stop.
    DisposableEffect(phase == CalPhase.COLLECTING, hasLocation) {
        if (phase != CalPhase.COLLECTING || !hasLocation) {
            onDispose {}
        } else {
            val lm = context.getSystemService(Context.LOCATION_SERVICE) as LocationManager
            val listener = object : LocationListener {
                override fun onLocationChanged(location: Location) {
                    if (location.hasSpeed()) gpsMph = location.speed * MS_TO_MPH
                }
            }
            try {
                lm.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000L, 0f, listener)
            } catch (_: SecurityException) {
            }
            onDispose { lm.removeUpdates(listener) }
        }
    }

    // Sample once a second while collecting: pair the latest GPS speed with the
    // latest raw count and re-fit. The calibrator drops sub-floor / zero-raw
    // pairs itself.
    LaunchedEffect(phase == CalPhase.COLLECTING) {
        if (phase != CalPhase.COLLECTING) return@LaunchedEffect
        while (true) {
            delay(1000)
            val mph = gpsMph
            val raw = TelemetryState.speedRaw
            if (mph != null && raw != null) {
                samples.add(SpeedCalibrator.Sample(raw, mph))
                result = SpeedCalibrator.compute(samples)
            }
        }
    }

    SectionCard(title = "Speed calibration") {
        Text(
            "Ride at a steady speed and correlate phone GPS with the raw ECM count to " +
                "solve the exact speed divisor, then write it back to the cluster. " +
                "Vary your pace between roughly 15 and 55 mph for the best fit.",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )

        appliedDivisor?.let { InfoRow("Active divisor", it.toString()) }

        when (phase) {
            CalPhase.IDLE -> {
                FilledTonalButton(
                    onClick = {
                        samples.clear()
                        result = null
                        gpsMph = null
                        if (hasLocation) {
                            phase = CalPhase.COLLECTING
                        } else {
                            permLauncher.launch(Manifest.permission.ACCESS_FINE_LOCATION)
                        }
                    },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Start calibration") }
            }

            CalPhase.COLLECTING -> {
                InfoRow("GPS speed", gpsMph?.let { "%.1f mph".format(it) } ?: "acquiring…")
                InfoRow("Raw count", TelemetryState.speedRaw?.toString() ?: "—")
                InfoRow("Samples", "${samples.size} (need ${SpeedCalibrator.MIN_SAMPLES})")
                result?.let {
                    InfoRow("Solved divisor", it.divisor.toString())
                    InfoRow("Fit error", "%.1f mph".format(it.rmsErrorMph))
                }
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    OutlinedButton(
                        onClick = { phase = CalPhase.IDLE },
                        modifier = Modifier.weight(1f),
                    ) { Text("Cancel") }
                    Button(
                        onClick = { phase = CalPhase.DONE },
                        enabled = result != null,
                        modifier = Modifier.weight(1f),
                    ) { Text("Finish") }
                }
            }

            CalPhase.DONE -> {
                val r = result
                if (r == null) {
                    phase = CalPhase.IDLE
                } else {
                    InfoRow("Solved divisor", r.divisor.toString())
                    InfoRow("Fit error", "%.1f mph".format(r.rmsErrorMph))
                    InfoRow("From samples", r.sampleCount.toString())
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                        OutlinedButton(
                            onClick = { phase = CalPhase.IDLE },
                            modifier = Modifier.weight(1f),
                        ) { Text("Redo") }
                        Button(
                            onClick = {
                                OutboundSink.send(Protocol.encodeConfig(r.divisor))
                                appliedDivisor = r.divisor
                                phase = CalPhase.IDLE
                            },
                            modifier = Modifier.weight(1f),
                        ) { Text("Apply to cluster") }
                    }
                }
            }
        }
    }
}
