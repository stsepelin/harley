package com.vrodcluster.companion.ui

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
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import com.vrodcluster.companion.R
import com.vrodcluster.companion.notif.AllowList
import com.vrodcluster.companion.notif.NotifAccess

@Composable
fun StatusScreen(onConfigureApps: () -> Unit) {
    val context = LocalContext.current
    val owner   = LocalLifecycleOwner.current

    // No callback fires when the user toggles notification access in
    // Settings, so we just re-check whenever we come back to the
    // foreground. Same trick covers any allowlist edits — the count
    // refreshes on resume from AppListScreen.
    var granted    by remember { mutableStateOf(NotifAccess.isGranted(context)) }
    var mutedCount by remember { mutableStateOf(AllowList.blocked(context).size) }
    DisposableEffect(owner) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_RESUME) {
                granted    = NotifAccess.isGranted(context)
                mutedCount = AllowList.blocked(context).size
            }
        }
        owner.lifecycle.addObserver(observer)
        onDispose { owner.lifecycle.removeObserver(observer) }
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

                Text(
                    if (granted) stringResource(R.string.notif_access_granted)
                    else         stringResource(R.string.notif_access_missing),
                    style = MaterialTheme.typography.bodyLarge,
                )

                if (!granted) {
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
            }
        }
    }
}
