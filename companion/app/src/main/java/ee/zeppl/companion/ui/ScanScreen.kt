package ee.zeppl.companion.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Button
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import ee.zeppl.companion.ble.BleScanner
import ee.zeppl.companion.ble.BleService
import ee.zeppl.companion.ble.BondTracker

/**
 * Active BLE scan with a tap-to-connect device list, reached from the Cluster
 * hub's "Add cluster". A top app bar (back), the scan status, a scrolling list
 * of nearby clusters as cards, and one full-width scan toggle at the bottom.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ScanScreen(onBack: () -> Unit) {
    val context = LocalContext.current
    val found = remember { mutableStateMapOf<String, BleScanner.ScannedDevice>() }
    var scanning by remember { mutableStateOf(true) }

    val scanner = remember {
        BleScanner(context.applicationContext) { dev -> found[dev.address] = dev }
    }
    DisposableEffect(Unit) {
        scanner.start()
        BondTracker.start(context.applicationContext)
        onDispose { scanner.stop() }
    }
    LaunchedEffect(scanning) { if (scanning) scanner.start() else scanner.stop() }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Add cluster") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                    }
                },
            )
        },
    ) { pad ->
        Column(
            Modifier
                .fillMaxSize()
                .padding(pad)
                .padding(horizontal = 20.dp)
                .padding(top = 8.dp, bottom = 20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            val bond = bondStatusText()
            if (bond.isNotEmpty()) {
                Text(
                    bond,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }

            val sorted = found.values.sortedByDescending { it.rssi }
            Box(Modifier.fillMaxWidth().weight(1f)) {
                if (sorted.isEmpty()) {
                    Column(
                        Modifier.align(Alignment.Center),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(12.dp),
                    ) {
                        if (scanning) CircularProgressIndicator(Modifier.size(28.dp))
                        Text(
                            if (scanning) "Scanning for clusters…" else "No clusters found.",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                } else {
                    Column(
                        Modifier.verticalScroll(rememberScrollState()),
                        verticalArrangement = Arrangement.spacedBy(10.dp),
                    ) {
                        sorted.forEach { dev ->
                            DeviceCard(dev) {
                                scanner.stop()
                                scanning = false
                                BleService.start(context, address = dev.address)
                                onBack()
                            }
                        }
                    }
                }
            }

            Button(
                onClick = { scanning = !scanning },
                modifier = Modifier.fillMaxWidth(),
            ) { Text(if (scanning) "Stop scanning" else "Scan again") }
        }
    }
}

@Composable
private fun DeviceCard(dev: BleScanner.ScannedDevice, onConnect: () -> Unit) {
    Card(
        onClick = onConnect,
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceContainerHigh,
        ),
    ) {
        Row(
            Modifier.padding(16.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text(dev.name ?: "(unnamed)", style = MaterialTheme.typography.bodyLarge)
                Text(
                    "${dev.address} • ${dev.rssi} dBm",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Text(
                "Connect",
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.primary,
            )
        }
    }
}

@Composable
private fun bondStatusText(): String {
    val state = BondTracker.bondState
    val addr = BondTracker.bondedAddress
    return when (state) {
        android.bluetooth.BluetoothDevice.BOND_BONDING ->
            "Pairing with ${addr ?: "device"}… confirm the matching code on the cluster"
        android.bluetooth.BluetoothDevice.BOND_BONDED ->
            "Paired with ${addr ?: "device"}"
        else -> ""
    }
}
