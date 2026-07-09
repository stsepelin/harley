package ee.zeppl.companion.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
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
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import ee.zeppl.companion.notif.AllowList
import ee.zeppl.companion.notif.NotifAccess

/**
 * App and cluster configuration. Phone-side relay settings (notification
 * access + per-app allowlist) work today; cluster-side config write-back
 * lands with Brick 3.
 */
@Composable
fun SettingsScreen(onConfigureApps: () -> Unit) {
    val context = LocalContext.current
    val owner = LocalLifecycleOwner.current

    // Notification access + allowlist are toggled in system Settings with no
    // callback; re-read on resume.
    var notifGranted by remember { mutableStateOf(NotifAccess.isGranted(context)) }
    var mutedCount by remember { mutableStateOf(AllowList.blocked(context).size) }
    DisposableEffect(owner) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_RESUME) {
                notifGranted = NotifAccess.isGranted(context)
                mutedCount = AllowList.blocked(context).size
            }
        }
        owner.lifecycle.addObserver(observer)
        onDispose { owner.lifecycle.removeObserver(observer) }
    }

    ScreenColumn(title = "Settings", subtitle = "Relay, appearance, and cluster configuration") {
        SectionCard(title = "Notification relay") {
            if (!notifGranted) {
                Text(
                    "Grant notification access so calls, messages, and media can appear on the cluster.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Button(
                    onClick = { context.startActivity(NotifAccess.grantIntent()) },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Grant notification access") }
            } else {
                InfoRow(
                    "Forwarding",
                    if (mutedCount == 0) "All apps" else "$mutedCount muted",
                )
                Button(onClick = onConfigureApps, modifier = Modifier.fillMaxWidth()) {
                    Text("Choose apps")
                }
            }
        }

        SectionCard(title = "Appearance") {
            SwitchRow(
                label = "Match device colors",
                sublabel = "Use your phone's system color scheme. Off keeps the Zeppl orange theme.",
                checked = AppPrefs.dynamicColor,
                onCheckedChange = { AppPrefs.dynamicColor = it },
            )
        }

        SectionCard(title = "Cluster configuration") {
            Text(
                "Units, display brightness, and alert thresholds will be editable here and written back to the cluster over the link (Brick 3). Every write is acknowledged and read back so you always know it took.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            InfoRow("Display units", "On cluster")
            InfoRow("Temperature units", "On cluster")
        }
    }
}

@Composable
private fun SwitchRow(
    label: String,
    sublabel: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
) {
    Row(
        Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(label, style = MaterialTheme.typography.bodyLarge)
            Text(
                sublabel,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        Switch(checked = checked, onCheckedChange = onCheckedChange)
    }
}
