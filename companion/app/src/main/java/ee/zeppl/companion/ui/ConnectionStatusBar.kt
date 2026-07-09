package ee.zeppl.companion.ui

import androidx.compose.animation.animateColorAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import ee.zeppl.companion.ble.BleConnState
import ee.zeppl.companion.ble.BleState
import ee.zeppl.companion.ui.theme.StatusColors

/**
 * Link status as a compact rounded pill - a small state dot carries the color
 * so "offline" reads as calm status, not an error. Baked into each page's
 * header (top-right, opposite the title) so status is visible everywhere
 * without a separate strip. Reads [BleState] directly, so it re-renders live.
 */
@Composable
fun ConnectionBadge(modifier: Modifier = Modifier) {
    val v = statusVisuals(BleState.conn, BleState.deviceName)
    val dot by animateColorAsState(v.color, label = "linkDot")

    Surface(
        shape = CircleShape,
        color = MaterialTheme.colorScheme.surfaceContainerHighest,
        modifier = modifier,
    ) {
        Row(
            Modifier.padding(horizontal = 14.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Spacer(Modifier.size(9.dp).clip(CircleShape).background(dot))
            Spacer(Modifier.width(10.dp))
            Text(
                v.label,
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

private data class LinkVisuals(val label: String, val color: Color)

private fun statusVisuals(state: BleConnState, name: String?): LinkVisuals {
    val who = name ?: "cluster"
    return when (state) {
        BleConnState.CONNECTED    -> LinkVisuals("Linked to $who", StatusColors.Linked)
        BleConnState.CONNECTING   -> LinkVisuals("Connecting to $who…", StatusColors.Warning)
        BleConnState.PAIRING      -> LinkVisuals("Pairing… confirm on cluster", StatusColors.Warning)
        BleConnState.SCANNING     -> LinkVisuals("Scanning…", StatusColors.Warning)
        BleConnState.WAITING      -> LinkVisuals("Reconnecting to $who…", StatusColors.Warning)
        BleConnState.DISCONNECTED -> LinkVisuals("Offline", StatusColors.Alert)
        BleConnState.IDLE         -> LinkVisuals("Not connected", StatusColors.Alert)
    }
}
