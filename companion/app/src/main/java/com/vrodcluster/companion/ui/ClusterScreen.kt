package com.vrodcluster.companion.ui

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
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
 * Device hub. Organized around the clusters themselves rather than loose
 * verbs: a connection card (with one-tap reconnect-to-latest), a list of
 * paired clusters each with its own Connect/Forget, and a single "Add cluster"
 * that opens the scan. No more separate Connect + Scan buttons.
 */
@SuppressLint("MissingPermission")
@Composable
fun ClusterScreen(onPickCluster: () -> Unit) {
    val context = LocalContext.current
    val owner = LocalLifecycleOwner.current

    var blePerms by remember { mutableStateOf(BleAccess.allGranted(context)) }
    var bonded by remember {
        mutableStateOf<List<BluetoothDevice>>(
            if (blePerms) BondedClusters.list(context) else emptyList()
        )
    }
    DisposableEffect(owner) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_RESUME) {
                blePerms = BleAccess.allGranted(context)
                bonded = if (blePerms) BondedClusters.list(context) else emptyList()
            }
        }
        owner.lifecycle.addObserver(observer)
        onDispose { owner.lifecycle.removeObserver(observer) }
    }

    // Re-list bonds whenever the link state changes - resume doesn't fire when
    // returning from the in-app scan, so a just-paired cluster would otherwise
    // not appear (or show as connected) until the app is backgrounded.
    LaunchedEffect(BleState.conn, blePerms) {
        if (blePerms) bonded = BondedClusters.list(context)
    }

    val perm = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { result ->
        blePerms = result.values.all { it }
        if (blePerms) bonded = BondedClusters.list(context)
    }

    // Non-null while the forget-confirmation dialog is up for that device.
    var forgetTarget by remember { mutableStateOf<BluetoothDevice?>(null) }
    // Android-style: tap the Firmware row seven times to unlock developer mode.
    var firmwareTaps by remember { mutableStateOf(0) }
    // One reused Toast so rapid taps update the visible message instead of
    // queuing a backlog of stale countdown toasts.
    val fwToast = remember { Toast.makeText(context, "", Toast.LENGTH_SHORT) }

    val connected = BleState.conn == BleConnState.CONNECTED
    val busy = BleState.conn in setOf(
        BleConnState.SCANNING, BleConnState.CONNECTING,
        BleConnState.PAIRING, BleConnState.WAITING,
    )
    val reconnectAddress = bonded.firstOrNull()?.address

    ScreenColumn(title = "Cluster", subtitle = "Your V-Rod instrument cluster") {
        SectionCard(title = "Connection") {
            InfoRow("Status", linkLabel(BleState.conn), valueColor = linkColor(BleState.conn))
            if (connected) BleState.deviceName?.let { InfoRow("Device", it) }

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
            } else if (connected) {
                OutlinedButton(
                    onClick = { BleService.stop(context) },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Disconnect") }
            } else if (busy) {
                // Give the rider a way out of a stuck connect/pair/scan.
                OutlinedButton(
                    onClick = { BleService.stop(context) },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Cancel") }
            } else if (reconnectAddress != null) {
                // Reconnect to the latest cluster by address with autoConnect, so
                // it works even when the cluster's visibility is off.
                Button(
                    onClick = { BleService.start(context, address = reconnectAddress, autoConnect = true) },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Reconnect") }
            }
        }

        if (blePerms) {
            SectionCard(title = "My clusters") {
                if (bonded.isEmpty()) {
                    Text(
                        "No clusters paired yet. Add one to connect.",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                } else {
                    bonded.forEach { dev ->
                        DeviceRow(
                            dev = dev,
                            connected = isConnectedTo(dev),
                            onConnect = {
                                BleService.start(context, address = dev.address, autoConnect = true)
                            },
                            onDisconnect = { BleService.stop(context) },
                            onForget = { forgetTarget = dev },
                        )
                    }
                }
                OutlinedButton(onClick = onPickCluster, modifier = Modifier.fillMaxWidth()) {
                    Icon(Icons.Filled.Add, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(8.dp))
                    Text("Add cluster")
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
            InfoRow("Firmware", "—", onClick = {
                if (AppPrefs.developerMode) {
                    fwToast.setText("Developer mode is already on")
                    fwToast.show()
                } else {
                    firmwareTaps++
                    val left = 7 - firmwareTaps
                    when {
                        left <= 0 -> {
                            AppPrefs.developerMode = true
                            fwToast.setText("Developer mode enabled")
                            fwToast.show()
                        }
                        left in 1..3 -> {
                            fwToast.setText(
                                "$left ${if (left == 1) "step" else "steps"} from developer mode",
                            )
                            fwToast.show()
                        }
                    }
                }
            })
        }
    }

    forgetTarget?.let { target ->
        AlertDialog(
            onDismissRequest = { forgetTarget = null },
            title = { Text("Forget this cluster?") },
            text = {
                Text(
                    "Removes the phone-side pairing with ${target.name ?: target.address}. " +
                        "You can add it again later.",
                )
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        BondedClusters.forget(target)
                        bonded = bonded - target
                        forgetTarget = null
                    },
                    colors = ButtonDefaults.textButtonColors(
                        contentColor = MaterialTheme.colorScheme.error,
                    ),
                ) { Text("Forget") }
            },
            dismissButton = {
                TextButton(onClick = { forgetTarget = null }) { Text("Cancel") }
            },
        )
    }
}

@SuppressLint("MissingPermission")
@Composable
private fun DeviceRow(
    dev: BluetoothDevice,
    connected: Boolean,
    onConnect: () -> Unit,
    onDisconnect: () -> Unit,
    onForget: () -> Unit,
) {
    Row(
        Modifier.fillMaxWidth().padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(dev.name ?: "Unnamed cluster", style = MaterialTheme.typography.bodyLarge)
            Text(
                dev.address,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        if (connected) {
            OutlinedButton(onClick = onDisconnect) { Text("Disconnect") }
        } else {
            FilledTonalButton(onClick = onConnect) { Text("Connect") }
        }
        Spacer(Modifier.width(8.dp))
        // Danger action: red outline + text, and it opens a confirmation.
        OutlinedButton(
            onClick = onForget,
            colors = ButtonDefaults.outlinedButtonColors(
                contentColor = MaterialTheme.colorScheme.error,
            ),
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.error),
        ) { Text("Forget") }
    }
}

@SuppressLint("MissingPermission")
private fun isConnectedTo(dev: BluetoothDevice): Boolean {
    if (BleState.conn != BleConnState.CONNECTED) return false
    val name = BleState.deviceName ?: return false
    return name == dev.name || name == dev.address
}

private fun linkLabel(state: BleConnState): String = when (state) {
    BleConnState.CONNECTED    -> "Linked"
    BleConnState.CONNECTING   -> "Connecting…"
    BleConnState.PAIRING      -> "Pairing…"
    BleConnState.SCANNING     -> "Scanning…"
    BleConnState.WAITING      -> "Reconnecting…"
    BleConnState.DISCONNECTED -> "Offline"
    BleConnState.IDLE         -> "Not connected"
}

private fun linkColor(state: BleConnState) = when (state) {
    BleConnState.CONNECTED -> StatusColors.Linked
    BleConnState.CONNECTING, BleConnState.PAIRING,
    BleConnState.SCANNING, BleConnState.WAITING -> StatusColors.Warning
    else -> StatusColors.Alert
}
