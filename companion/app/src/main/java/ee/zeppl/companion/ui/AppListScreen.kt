package ee.zeppl.companion.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import ee.zeppl.companion.R
import ee.zeppl.companion.notif.AllowList
import ee.zeppl.companion.notif.AppInfo
import ee.zeppl.companion.notif.AppListProvider
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AppListScreen(onBack: () -> Unit) {
    val context = LocalContext.current

    // Enumeration + icon rasterization can take ~1s on a phone with
    // hundreds of apps; do it off the main thread and show a spinner
    // until it lands. Null = still loading.
    val apps by produceState<List<AppInfo>?>(initialValue = null) {
        value = withContext(Dispatchers.IO) { AppListProvider.list(context) }
    }

    // Mute set; UI flips this synchronously on each toggle so the
    // Switch never lags a SharedPrefs round-trip.
    var blocked by remember { mutableStateOf(AllowList.blocked(context)) }

    BackHandler(onBack = onBack)

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.app_list_title)) },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack,
                             contentDescription = stringResource(R.string.back))
                    }
                },
            )
        }
    ) { pad ->
        val current = apps
        if (current == null) {
            Box(Modifier.fillMaxSize().padding(pad), contentAlignment = Alignment.Center) {
                CircularProgressIndicator()
            }
            return@Scaffold
        }

        LazyColumn(Modifier.fillMaxSize().padding(pad)) {
            items(current, key = { it.packageName }) { app ->
                AppRow(
                    app     = app,
                    allowed = app.packageName !in blocked,
                    onToggle = { on ->
                        AllowList.set(context, app.packageName, on)
                        blocked = AllowList.blocked(context)
                    },
                )
                HorizontalDivider()
            }
        }
    }
}

@Composable
private fun AppRow(app: AppInfo, allowed: Boolean, onToggle: (Boolean) -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment     = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Image(bitmap = app.icon,
              contentDescription = null,
              modifier = Modifier.size(40.dp))
        Column(Modifier.weight(1f)) {
            Text(app.label,
                 style = MaterialTheme.typography.bodyLarge)
            Text(app.packageName,
                 style = MaterialTheme.typography.bodySmall,
                 color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Switch(checked = allowed, onCheckedChange = onToggle)
    }
}
