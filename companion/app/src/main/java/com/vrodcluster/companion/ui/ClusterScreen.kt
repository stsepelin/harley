package com.vrodcluster.companion.ui

import android.bluetooth.BluetoothDevice
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
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.vrodcluster.companion.ble.BleAccess
import com.vrodcluster.companion.ble.BleConnState
import com.vrodcluster.companion.ble.BleService
import com.vrodcluster.companion.ble.BleState
import com.vrodcluster.companion.ble.BondedClusters
import com.vrodcluster.companion.ui.theme.StatusColors

/**
 * Device hub: the link, paired-device management, and entry points to
 * setup/maintenance. The old StatusScreen crammed this together with the
 * notification-relay controls; those now live under Settings, leaving this
 * page focused on "my cluster".
 */
@Composable
fun ClusterScreen(onPickCluster: () -> Unit) {
    val context = LocalContext.current
    val owner = LocalLifecycleOwner.current

    var blePerms by remember { mutableStateOf(BleAccess.allGranted(context)) }
    var bondedClusters by remember {
        mutableStateOf<List<BluetoothDevice>>(
            if (blePerms) BondedClusters.list(context) else emptyList()
        )
    }
    // Bond state changes via system events with no Compose observable; refresh
    // on resume, the natural return point.
    DisposableEffect(owner) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_RESUME) {
                blePerms = BleAccess.allGranted(context)
                bondedClusters = if (blePerms) BondedClusters.list(context) else emptyList()
            }
        }
        owner.lifecycle.addObserver(observer)
        onDispose { owner.lifecycle.removeObserver(observer) }
    }

    val perm = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { result ->
        blePerms = result.values.all { it }
        if (blePerms) BleService.start(context)
    }

    ScreenColumn(title = "Cluster", subtitle = "Your V-Rod instrument cluster") {
        SectionCard(title = "Link") {
            InfoRow("Status", linkLabel(BleState.conn), valueColor = linkColor(BleState.conn))
            BleState.deviceName?.let { InfoRow("Device", it) }

            if (!blePerms) {
                Text(
                    "Bluetooth permission is needed to find and connect to the cluster.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Button(
                    onClick = { perm.launch(BleAccess.REQUIRED) },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Grant Bluetooth permission") }
            } else {
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    when (BleState.conn) {
                        BleConnState.IDLE, BleConnState.DISCONNECTED ->
                            Button(onClick = onPickCluster, modifier = Modifier.weight(1f)) {
                                Text("Connect")
                            }
                        else ->
                            OutlinedButton(
                                onClick = { BleService.stop(context) },
                                modifier = Modifier.weight(1f),
                            ) { Text("Disconnect") }
                    }
                    FilledTonalButton(onClick = onPickCluster, modifier = Modifier.weight(1f)) {
                        Text("Scan")
                    }
                }
            }
        }

        if (bondedClusters.isNotEmpty()) {
            SectionCard(title = "Paired with this phone") {
                Text(
                    "Removes the phone-side bond. Use this if the cluster forgot you but the phone still has it paired.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                bondedClusters.forEach { dev ->
                    val label = dev.name ?: dev.address
                    Row(
                        Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                    ) {
                        Text(label, style = MaterialTheme.typography.bodyLarge)
                        OutlinedButton(onClick = {
                            BondedClusters.forget(dev)
                            bondedClusters = bondedClusters - dev
                        }) { Text("Forget") }
                    }
                }
            }
        }

        SectionCard(title = "Setup & maintenance") {
            Text(
                "Speed calibration, fault-code diagnostics, and firmware come online with the next bricks — they need a live link to the cluster.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            InfoRow("Speed calibration", "Coming soon")
            InfoRow("Diagnostics (DTC)", "Coming soon")
            InfoRow("Firmware", "—")
        }
    }
}

private fun linkLabel(state: BleConnState): String = when (state) {
    BleConnState.CONNECTED    -> "Linked"
    BleConnState.CONNECTING   -> "Connecting…"
    BleConnState.SCANNING     -> "Scanning…"
    BleConnState.WAITING      -> "Reconnecting…"
    BleConnState.DISCONNECTED -> "Offline"
    BleConnState.IDLE         -> "Not connected"
}

private fun linkColor(state: BleConnState) = when (state) {
    BleConnState.CONNECTED -> StatusColors.Linked
    BleConnState.CONNECTING, BleConnState.SCANNING, BleConnState.WAITING -> StatusColors.Warning
    else -> StatusColors.Alert
}
