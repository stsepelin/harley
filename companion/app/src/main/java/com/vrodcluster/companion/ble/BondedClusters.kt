package com.vrodcluster.companion.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.content.Context
import android.util.Log

/**
 * Phone-side bond management for V-Rod clusters.
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
 * bonded with the name "V-Rod Cluster" and remove the bond.
 *
 * [BluetoothDevice.removeBond] is a hidden API ([@hide]) but has
 * been stable since API 1 and is accessible via reflection. Most
 * production Android apps that need this functionality (Wear, fitness
 * trackers, etc.) use the same pattern.
 */
object BondedClusters {

    private const val TAG          = "BondedClusters"
    private const val CLUSTER_NAME = "V-Rod Cluster"

    /**
     * Bonded BluetoothDevices that look like V-Rod clusters. Matched by
     * the cached device name the OS stored at pairing time. Not perfect
     * — a hostile or malformed peripheral could spoof the name — but
     * good enough for the companion UI's "forget" affordance, where
     * the worst case is the user unpairs a device they didn't expect.
     */
    @SuppressLint("MissingPermission")
    fun list(context: Context): List<BluetoothDevice> {
        val adapter = context.getSystemService(BluetoothManager::class.java).adapter
            ?: return emptyList()
        val bonded = adapter.bondedDevices ?: return emptyList()
        return bonded.filter { it.name == CLUSTER_NAME }
    }

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
