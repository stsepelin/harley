package ee.zeppl.companion.ui

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.foundation.text.KeyboardOptions
import ee.zeppl.companion.ble.TelemetryState
import ee.zeppl.companion.cal.FuelEconomy

private const val DASH = "—"

/**
 * Live fuel economy + range-to-empty for the Ride dashboard (Brick 4). Reads
 * the current trip's distance and fuel-tick counter from [TelemetryState],
 * turns them into mpg via the persisted [FuelPrefs] calibration, and estimates
 * range from the level sender. A fill-up dialog re-solves mL/tick from litres
 * added over the trip's counted ticks (reset Trip 1 at the previous fill).
 */
@Composable
fun FuelEconomyCard() {
    val context = LocalContext.current
    var mlPerTick by remember { mutableStateOf(FuelPrefs.mlPerTick(context)) }
    var showDialog by remember { mutableStateOf(false) }

    val tripMeters = TelemetryState.trip1M
    val tripTicks = TelemetryState.trip1FuelTicks
    val level = TelemetryState.fuelLevel

    val economy = if (mlPerTick > 0.0 && tripMeters != null && tripTicks != null) {
        FuelEconomy.economy(tripMeters, tripTicks, mlPerTick)
    } else {
        null
    }
    val rangeMiles = if (economy != null && level != null) {
        FuelEconomy.rangeMiles(FuelEconomy.litersFromLevel(level), economy.kmPerLiter)
    } else {
        null
    }

    SectionCard(title = "Fuel") {
        InfoRow("Level", level?.let { "$it / ${FuelEconomy.FUEL_LEVEL_MAX}" } ?: DASH)

        if (mlPerTick <= 0.0) {
            Text(
                "Calibrate at your next fill-up to unlock economy and range.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        } else {
            InfoRow(
                "Trip economy",
                economy?.let { "%.1f mpg  ·  %.1f L/100km".format(it.mpgUs, it.litersPer100km) } ?: DASH,
            )
            InfoRow("Range", rangeMiles?.let { "%.0f mi".format(it) } ?: DASH)
        }

        FilledTonalButton(
            onClick = { showDialog = true },
            modifier = Modifier.fillMaxWidth(),
        ) { Text(if (mlPerTick > 0.0) "Recalibrate at fill-up" else "Calibrate at fill-up") }
    }

    if (showDialog) {
        FillUpDialog(
            ticks = tripTicks,
            onDismiss = { showDialog = false },
            onCalibrated = {
                FuelPrefs.setMlPerTick(context, it)
                mlPerTick = it
                showDialog = false
            },
        )
    }
}

@Composable
private fun FillUpDialog(
    ticks: Long?,
    onDismiss: () -> Unit,
    onCalibrated: (Double) -> Unit,
) {
    var litersText by remember { mutableStateOf("") }
    val liters = litersText.toDoubleOrNull()
    val result = if (liters != null && ticks != null) {
        FuelEconomy.calibrateMlPerTick(liters, ticks)
    } else {
        null
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Fill-up calibration") },
        text = {
            androidx.compose.foundation.layout.Column(
                verticalArrangement = androidx.compose.foundation.layout.Arrangement.spacedBy(12.dp),
            ) {
                Text(
                    "Fill the tank, then enter the litres you added. This divides them " +
                        "across the ${ticks ?: DASH} fuel ticks counted on Trip 1 since your " +
                        "last fill - so reset Trip 1 each time you fill up.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                OutlinedTextField(
                    value = litersText,
                    onValueChange = { litersText = it },
                    label = { Text("Litres added") },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                    modifier = Modifier.fillMaxWidth(),
                )
                if (result != null) {
                    Text(
                        "= %.3f mL/tick".format(result),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.primary,
                    )
                }
            }
        },
        confirmButton = {
            TextButton(
                onClick = { result?.let(onCalibrated) },
                enabled = result != null,
            ) { Text("Save") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}
