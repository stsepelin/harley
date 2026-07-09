package ee.zeppl.companion.ble

import android.content.Context

/**
 * Per-cluster display-name overrides, keyed by MAC address. The OS device name
 * ("Zeppl") is shared across every unit, so a rider with more than one
 * needs a way to tell them apart - "Muscle", "Night Rod", etc. Stored app-side
 * (not via BluetoothDevice.setAlias) so it stays scoped to this app and never
 * touches the system bond record.
 */
object ClusterNames {

    private const val PREFS = "zeppl.cluster_names"

    /** The user-set name for [address], or null if none. */
    fun custom(context: Context, address: String): String? =
        prefs(context).getString(address, null)?.takeIf { it.isNotBlank() }

    /** The name to show: user override if set, else [fallback] (the OS name). */
    fun display(context: Context, address: String, fallback: String?): String =
        custom(context, address) ?: fallback?.takeIf { it.isNotBlank() } ?: "Unnamed cluster"

    fun setName(context: Context, address: String, name: String) {
        val trimmed = name.trim()
        prefs(context).edit().apply {
            if (trimmed.isEmpty()) remove(address) else putString(address, trimmed)
        }.apply()
    }

    private fun prefs(context: Context) =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
}
