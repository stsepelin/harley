package com.vrodcluster.companion.ui

import androidx.compose.animation.animateColorAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.BluetoothSearching
import androidx.compose.material.icons.filled.BluetoothConnected
import androidx.compose.material.icons.filled.BluetoothDisabled
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.vrodcluster.companion.ble.BleConnState
import com.vrodcluster.companion.ble.BleState
import com.vrodcluster.companion.ui.theme.StatusColors

/**
 * Slim, always-present link-status strip. The one thing every failed
 * motorcycle companion app gets wrong is hiding or lying about connection
 * state; this stays visible on every tab and tapping it jumps to the Cluster
 * hub to act on the link. Reads [BleState] directly, so it re-renders live.
 */
@Composable
fun ConnectionStatusBar(onManage: () -> Unit, modifier: Modifier = Modifier) {
    val v = statusVisuals(BleState.conn, BleState.deviceName)
    val dot by animateColorAsState(v.color, label = "linkDot")

    Surface(
        color = MaterialTheme.colorScheme.surfaceContainerLow,
        modifier = modifier.fillMaxWidth(),
    ) {
        Row(
            Modifier
                .fillMaxWidth()
                .clickable(onClick = onManage)
                .statusBarsPadding()
                .padding(horizontal = 20.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(v.icon, contentDescription = null, tint = dot, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(10.dp))
            Text(
                v.label,
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.weight(1f),
            )
            if (v.pulsing) {
                // A small filled dot doubles as the "working" affordance next
                // to the label without pulling in a spinner.
                Spacer(
                    Modifier
                        .size(8.dp)
                        .clip(CircleShape)
                        .background(dot),
                )
            }
        }
    }
}

private data class LinkVisuals(
    val label: String,
    val color: Color,
    val icon: ImageVector,
    val pulsing: Boolean,
)

private fun statusVisuals(state: BleConnState, name: String?): LinkVisuals {
    val who = name ?: "cluster"
    return when (state) {
        BleConnState.CONNECTED ->
            LinkVisuals("Linked to $who", StatusColors.Linked, Icons.Filled.BluetoothConnected, false)
        BleConnState.CONNECTING ->
            LinkVisuals("Connecting to $who…", StatusColors.Warning, Icons.AutoMirrored.Filled.BluetoothSearching, true)
        BleConnState.SCANNING ->
            LinkVisuals("Scanning…", StatusColors.Warning, Icons.AutoMirrored.Filled.BluetoothSearching, true)
        BleConnState.WAITING ->
            LinkVisuals("Reconnecting to $who…", StatusColors.Warning, Icons.AutoMirrored.Filled.BluetoothSearching, true)
        BleConnState.DISCONNECTED ->
            LinkVisuals("Offline — tap to reconnect", StatusColors.Alert, Icons.Filled.BluetoothDisabled, false)
        BleConnState.IDLE ->
            LinkVisuals("Not connected — tap to set up", StatusColors.Alert, Icons.Filled.BluetoothDisabled, false)
    }
}
