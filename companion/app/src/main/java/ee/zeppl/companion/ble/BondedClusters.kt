package ee.zeppl.companion.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.content.Context
import android.util.Log

/**
 * Phone-side bond management for Zeppl clusters.
 *
 * The cluster's BT subpage exposes "Forget all devices" which clears
 * the *cluster's* NVS-stored bond. That's only half the symmetry —
 * the phone still has its own bond record for the cluster's MAC,
 * and Samsung in particular hides paired BLE peripherals from the
 * "Pair new device" scan list, so once a stale bond exists on the
 * phone you can't easily get rid of it through the OS settings.
 *
 * This module lets the companion app do the equivalent operation
 * on the phone side: find every BluetoothDevice the OS considers
 * bonded with the name "Zeppl" and remove the bond.
 *
 * [BluetoothDevice.removeBond] is a hidden API ([@hide]) but has
 * been stable since API 1 and is accessible via reflection. Most
 * production Android apps that need this functionality (Wear, fitness
 * trackers, etc.) use the same pattern.
 */
object BondedClusters {

    private const val TAG = "BondedClusters"

    /**
     * Bonded BluetoothDevices that *might* be a Zeppl cluster. The
     * earlier exact-name filter missed real
     * bonds on Samsung phones, which sometimes cache an alias or
     * truncated name at pairing time; the user-facing symptom was
     * "the Forget button never shows up". The current filter is
     * deliberately permissive:
     *
     *   - device type is LE or DUAL (no classic-only BR/EDR speakers),
     *   - cached name OR alias contains "zeppl" (case-insensitive), OR
     *     the entry has no name at all (some BLE bonds keep only the
     *     MAC, and we'd rather list it than silently hide it).
     *
     * The trade-off: a user with another BLE peripheral whose name
     * happens to share the "zeppl" substring would see it here. Worst
     * case the user unpairs an unrelated device — recoverable by
     * re-pairing it through their phone's normal flow.
     */
    @SuppressLint("MissingPermission")
    fun list(context: Context): List<BluetoothDevice> {
        val adapter = context.getSystemService(BluetoothManager::class.java).adapter
            ?: return emptyList()
        val bonded = adapter.bondedDevices ?: return emptyList()
        // Diagnostic: log every bonded device so a user reporting "no
        // Forget button" can attach a logcat that shows what the OS
        // actually has on hand, including names and types. Cheap; runs
        // once on screen show + lifecycle resume.
        for (d in bonded) {
            Log.i(TAG, "bonded: addr=${d.address} name='${d.name}' " +
                       "alias='${runCatching { d.alias }.getOrNull()}' type=${d.type}")
        }
        return bonded.filter { dev ->
            val isLe = dev.type == BluetoothDevice.DEVICE_TYPE_LE ||
                       dev.type == BluetoothDevice.DEVICE_TYPE_DUAL ||
                       dev.type == BluetoothDevice.DEVICE_TYPE_UNKNOWN
            if (!isLe) return@filter false
            val name  = dev.name?.lowercase().orEmpty()
            val alias = runCatching { dev.alias?.lowercase().orEmpty() }.getOrDefault("")
            // Empty name + LE/DUAL: probably a fresh bond before the
            // remote name resolved. Worth surfacing so the user can
            // forget it if it's actually the cluster's MAC.
            val hasNoName = dev.name.isNullOrBlank() && alias.isBlank()
            "zeppl" in name || "zeppl" in alias ||
                hasNoName
        }
    }

    /** The bonded cluster with this address, or null if it isn't paired. */
    @SuppressLint("MissingPermission")
    fun byAddress(context: Context, address: String): BluetoothDevice? =
        list(context).firstOrNull { it.address == address }

    /**
     * Remove the OS-side bond for the given device. Returns true if the
     * removal request was accepted (the actual unbond completes
     * asynchronously and broadcasts [BluetoothDevice.ACTION_BOND_STATE_CHANGED]).
     */
    fun forget(device: BluetoothDevice): Boolean {
        return try {
            val method = device.javaClass.getMethod("removeBond")
            val result = method.invoke(device)
            (result as? Boolean) ?: false
        } catch (t: Throwable) {
            Log.w(TAG, "removeBond reflection failed: $t")
            false
        }
    }
}
