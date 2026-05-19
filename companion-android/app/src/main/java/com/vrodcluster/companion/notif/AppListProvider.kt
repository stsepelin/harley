package com.vrodcluster.companion.notif

import android.content.Context
import android.content.pm.ApplicationInfo
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.core.graphics.drawable.toBitmap

data class AppInfo(
    val packageName: String,
    val label:       String,
    val icon:        ImageBitmap,
)

/**
 * Enumerates installed packages relevant to notification forwarding,
 * with rasterized icons sized to the device density.
 *
 * Uses `getInstalledPackages` (gated by QUERY_ALL_PACKAGES) rather than
 * the narrower MAIN/LAUNCHER query: the Phone / Messages / Dialer apps
 * post important notifications but often don't expose a launcher
 * activity, so a launcher-only query would silently hide them.
 *
 * We still filter to keep the list useful:
 *   - Drop pure system apps (`FLAG_SYSTEM` set, `FLAG_UPDATED_SYSTEM_APP`
 *     not set) — these are kernels of UI like SystemUI providers, not
 *     things the user would knowingly toggle. Updated-system apps
 *     (Phone, Messages, Gmail, Maps) shipped pre-installed but receive
 *     OTA updates so the user thinks of them as "apps" — keep them.
 *   - Drop entries without a human-readable label.
 *   - Drop apps whose icon fails to render (rare; treats them as junk).
 *   - Drop our own package.
 *
 * This is the slow path on a phone with hundreds of apps — the caller
 * should run it on Dispatchers.IO and show a spinner.
 */
object AppListProvider {

    fun list(context: Context): List<AppInfo> {
        val pm     = context.packageManager
        // 48dp at the device's density. Coerced because some emulators
        // report fractional density values that round to 0.
        val sizePx = (48 * context.resources.displayMetrics.density).toInt().coerceAtLeast(48)

        return pm.getInstalledPackages(0)
            .asSequence()
            .mapNotNull { pkg ->
                val ai = pkg.applicationInfo ?: return@mapNotNull null
                if (!shouldShow(ai)) return@mapNotNull null
                val label = ai.loadLabel(pm).toString()
                if (label.isBlank()) return@mapNotNull null
                val icon = runCatching {
                    ai.loadIcon(pm).toBitmap(sizePx, sizePx).asImageBitmap()
                }.getOrNull() ?: return@mapNotNull null
                AppInfo(pkg.packageName, label, icon)
            }
            .filter { it.packageName != context.packageName }
            .distinctBy { it.packageName }
            .sortedBy { it.label.lowercase() }
            .toList()
    }

    private fun shouldShow(ai: ApplicationInfo): Boolean {
        val isSystem        = ai.flags and ApplicationInfo.FLAG_SYSTEM              != 0
        val isUpdatedSystem = ai.flags and ApplicationInfo.FLAG_UPDATED_SYSTEM_APP  != 0
        return !isSystem || isUpdatedSystem
    }
}
