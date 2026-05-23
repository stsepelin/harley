package com.vrodcluster.companion.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import android.bluetooth.BluetoothDevice
import com.vrodcluster.companion.R
import com.vrodcluster.companion.ble.BleAccess
import com.vrodcluster.companion.ble.BleConnState
import com.vrodcluster.companion.ble.BleService
import com.vrodcluster.companion.ble.BleState
import com.vrodcluster.companion.ble.BondedClusters
import com.vrodcluster.companion.notif.AllowList
import com.vrodcluster.companion.notif.NotifAccess

@Composable
fun StatusScreen(onConfigureApps: () -> Unit) {
    val context = LocalContext.current
    val owner   = LocalLifecycleOwner.current

    // Re-check Settings-managed grants whenever we come back to the
    // foreground. Notification access has no callback when toggled in
    // Settings, and the user might also flip an app's allowlist row
    // before returning here.
    var notifGranted by remember { mutableStateOf(NotifAccess.isGranted(context)) }
    var mutedCount   by remember { mutableStateOf(AllowList.blocked(context).size) }
    var blePerms     by remember { mutableStateOf(BleAccess.allGranted(context)) }
    // Re-list on resume. The phone-side bond state can change via
    // Android system events (e.g. user unpaired from Settings, or our
    // own removeBond completed asynchronously), and there's no Compose
    // observable for it; resume is the natural refresh point.
    var bondedClusters by remember {
        mutableStateOf<List<BluetoothDevice>>(
            if (BleAccess.allGranted(context)) BondedClusters.list(context) else emptyList()
        )
    }
    DisposableEffect(owner) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_RESUME) {
                notifGranted   = NotifAccess.isGranted(context)
                mutedCount     = AllowList.blocked(context).size
                blePerms       = BleAccess.allGranted(context)
                bondedClusters = if (blePerms) BondedClusters.list(context) else emptyList()
            }
        }
        owner.lifecycle.addObserver(observer)
        onDispose { owner.lifecycle.removeObserver(observer) }
    }

    // Multi-permission grant for BLE_SCAN + BLE_CONNECT + POST_NOTIFICATIONS.
    val perm = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { result ->
        blePerms = result.values.all { it }
        if (blePerms) BleService.start(context)
    }

    Scaffold { pad ->
        Box(
            Modifier.fillMaxSize().padding(pad),
            contentAlignment = Alignment.Center,
        ) {
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Text(stringResource(R.string.app_name),
                     style = MaterialTheme.typography.headlineMedium)

                // --- notification access section --------------------
                Text(
                    if (notifGranted) stringResource(R.string.notif_access_granted)
                    else              stringResource(R.string.notif_access_missing),
                    style = MaterialTheme.typography.bodyLarge,
                )
                if (!notifGranted) {
                    Button(onClick = { context.startActivity(NotifAccess.grantIntent()) }) {
                        Text(stringResource(R.string.grant_notif_access))
                    }
                } else {
                    Text(
                        if (mutedCount == 0) stringResource(R.string.forwarding_all)
                        else                 stringResource(R.string.forwarding_some_muted, mutedCount),
                        style = MaterialTheme.typography.bodyMedium,
                    )
                    Button(onClick = onConfigureApps) {
                        Text(stringResource(R.string.configure_apps))
                    }
                }

                // --- BLE link section -------------------------------
                Text(bleStatusText(),
                     style = MaterialTheme.typography.bodyLarge)
                if (!blePerms) {
                    Text(stringResource(R.string.ble_perms_missing),
                         style = MaterialTheme.typography.bodySmall,
                         color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Button(onClick = { perm.launch(BleAccess.REQUIRED) }) {
                        Text(stringResource(R.string.grant_ble_perms))
                    }
                } else {
                    when (BleState.conn) {
                        BleConnState.IDLE,
                        BleConnState.DISCONNECTED -> Button(onClick = { BleService.start(context) }) {
                            Text(stringResource(R.string.connect_cluster))
                        }
                        else -> Button(onClick = { BleService.stop(context) }) {
                            Text(stringResource(R.string.disconnect_cluster))
                        }
                    }

                    // Paired-cluster forget panel. Only renders when the
                    // phone actually has bonds for cluster-shaped devices,
                    // so the steady-state UI stays uncluttered. Useful
                    // when the cluster's NVS got wiped but the phone
                    // still has the bond (Samsung's BT settings hides
                    // the cluster in that state, so this is the only
                    // way to recover without diving into developer
                    // options).
                    if (bondedClusters.isNotEmpty()) {
                        Text(
                            stringResource(R.string.paired_clusters_label),
                            style = MaterialTheme.typography.bodyMedium,
                        )
                        Text(
                            stringResource(R.string.forget_paired_cluster_hint),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                        bondedClusters.forEach { dev ->
                            val label = dev.name ?: dev.address
                            Button(onClick = {
                                BondedClusters.forget(dev)
                                // Optimistic: drop it from the list right
                                // away. The system broadcasts ACTION_BOND_STATE_CHANGED
                                // asynchronously, but we'd need a receiver
                                // for it; resume-refresh covers the
                                // common case where the user backgrounds
                                // and returns.
                                bondedClusters = bondedClusters - dev
                            }) {
                                Text(stringResource(R.string.forget_paired_cluster, label))
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun bleStatusText(): String = when (BleState.conn) {
    BleConnState.IDLE         -> stringResource(R.string.ble_state_idle)
    BleConnState.SCANNING     -> stringResource(R.string.ble_state_scanning)
    BleConnState.CONNECTING   -> stringResource(R.string.ble_state_connecting, BleState.deviceName ?: "…")
    BleConnState.CONNECTED    -> stringResource(R.string.ble_state_connected,  BleState.deviceName ?: "?")
    BleConnState.DISCONNECTED -> stringResource(R.string.ble_state_disconnected)
}
