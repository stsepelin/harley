package ee.zeppl.companion.ui

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.widget.Toast
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
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
import ee.zeppl.companion.ble.BleConnState
import ee.zeppl.companion.ble.BleService
import ee.zeppl.companion.ble.BleState
import ee.zeppl.companion.ble.BondedClusters
import ee.zeppl.companion.ble.ClusterNames
import ee.zeppl.companion.ui.theme.StatusColors

/**
 * Per-cluster detail, pushed from a row on the Cluster hub. Home for everything
 * scoped to one unit: rename, connect/disconnect, firmware, and diagnostics.
 * The developer-mode unlock (tap Firmware seven times) lives here now that each
 * cluster carries its own firmware section.
 */
@OptIn(ExperimentalMaterial3Api::class)
@SuppressLint("MissingPermission")
@Composable
fun ClusterDetailScreen(address: String, onBack: () -> Unit) {
    val context = LocalContext.current
    val owner = LocalLifecycleOwner.current

    var device by remember { mutableStateOf<BluetoothDevice?>(BondedClusters.byAddress(context, address)) }
    var name by remember { mutableStateOf(ClusterNames.display(context, address, device?.name)) }
    DisposableEffect(owner) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_RESUME) {
                device = BondedClusters.byAddress(context, address)
                name = ClusterNames.display(context, address, device?.name)
            }
        }
        owner.lifecycle.addObserver(observer)
        onDispose { owner.lifecycle.removeObserver(observer) }
    }

    var showRename by remember { mutableStateOf(false) }
    var showForget by remember { mutableStateOf(false) }
    var firmwareTaps by remember { mutableStateOf(0) }
    val fwToast = remember { Toast.makeText(context, "", Toast.LENGTH_SHORT) }

    val connectedToThis = BleState.conn == BleConnState.CONNECTED &&
        (BleState.deviceName == device?.name || BleState.deviceName == address)

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(name) },
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
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 20.dp)
                .padding(top = 8.dp, bottom = 24.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            SectionCard(title = "Identity") {
                InfoRow("Name", name)
                InfoRow("Address", address)
                OutlinedButton(onClick = { showRename = true }, modifier = Modifier.fillMaxWidth()) {
                    Text("Rename")
                }
            }

            SectionCard(title = "Connection") {
                InfoRow(
                    "Status",
                    if (connectedToThis) "Linked" else "Offline",
                    valueColor = if (connectedToThis) StatusColors.Linked else StatusColors.Alert,
                )
                if (connectedToThis) {
                    OutlinedButton(
                        onClick = { BleService.stop(context) },
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text("Disconnect") }
                } else {
                    Button(
                        onClick = { BleService.start(context, address = address, autoConnect = true) },
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text("Connect") }
                }
            }

            SectionCard(title = "Firmware") {
                InfoRow("Version", if (connectedToThis) "Unknown" else "—", onClick = {
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
                                fwToast.setText("$left ${if (left == 1) "step" else "steps"} from developer mode")
                                fwToast.show()
                            }
                        }
                    }
                })
                Text(
                    "Over-the-air firmware updates arrive in a later phase. Tap Version to reveal developer tools.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }

            SectionCard(title = "Diagnostics") {
                InfoRow("Fault codes (DTC)", "Coming soon")
                Text(
                    "Read and clear stored diagnostic trouble codes once the cluster exposes them over the link.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }

            OutlinedButton(
                onClick = { showForget = true },
                modifier = Modifier.fillMaxWidth(),
                colors = ButtonDefaults.outlinedButtonColors(
                    contentColor = MaterialTheme.colorScheme.error,
                ),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.error),
            ) { Text("Forget this cluster") }
        }
    }

    if (showRename) {
        RenameDialog(
            current = ClusterNames.custom(context, address) ?: device?.name.orEmpty(),
            onDismiss = { showRename = false },
            onSave = {
                ClusterNames.setName(context, address, it)
                name = ClusterNames.display(context, address, device?.name)
                showRename = false
            },
        )
    }

    if (showForget) {
        AlertDialog(
            onDismissRequest = { showForget = false },
            title = { Text("Forget this cluster?") },
            text = { Text("Removes the phone-side pairing with $name. You can add it again later.") },
            confirmButton = {
                TextButton(
                    onClick = {
                        device?.let { BondedClusters.forget(it) }
                        showForget = false
                        onBack()
                    },
                    colors = ButtonDefaults.textButtonColors(
                        contentColor = MaterialTheme.colorScheme.error,
                    ),
                ) { Text("Forget") }
            },
            dismissButton = { TextButton(onClick = { showForget = false }) { Text("Cancel") } },
        )
    }
}

@Composable
private fun RenameDialog(current: String, onDismiss: () -> Unit, onSave: (String) -> Unit) {
    var text by remember { mutableStateOf(current) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Rename cluster") },
        text = {
            OutlinedTextField(
                value = text,
                onValueChange = { text = it },
                label = { Text("Cluster name") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth(),
            )
        },
        confirmButton = { TextButton(onClick = { onSave(text) }) { Text("Save") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}
