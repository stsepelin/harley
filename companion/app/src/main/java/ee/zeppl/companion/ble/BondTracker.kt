package ee.zeppl.companion.ble

import android.bluetooth.BluetoothDevice
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/**
 * Tracks the Android bond state for the currently-connecting cluster so
 * the UI can show "pairing…", "paired", and pairing failures without
 * polling. Compose state is exposed via [bondState] / [bondedAddress]
 * for direct observation; the receiver self-registers when [start] is
 * called and unregisters on [stop].
 */
object BondTracker {

    /** What the Android stack currently thinks the bond is doing. */
    var bondState by mutableStateOf(BluetoothDevice.BOND_NONE)
        private set

    /** Address (MAC) of the device whose bond state we last observed. */
    var bondedAddress by mutableStateOf<String?>(null)
        private set

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action != BluetoothDevice.ACTION_BOND_STATE_CHANGED) return
            val dev: BluetoothDevice? = intent.getParcelableExtra(
                BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
            val state = intent.getIntExtra(
                BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.BOND_NONE)
            Log.i(TAG, "bond ${dev?.address} state=$state")
            bondState     = state
            bondedAddress = dev?.address
        }
    }

    private var registered = false

    fun start(context: Context) {
        if (registered) return
        // RECEIVER_NOT_EXPORTED: this is an internal broadcast we only
        // ever receive, not send. Marking it explicit on API 33+ is the
        // current requirement; older APIs ignore the flag.
        context.applicationContext.registerReceiver(
            receiver,
            IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED),
            Context.RECEIVER_NOT_EXPORTED,
        )
        registered = true
    }

    fun stop(context: Context) {
        if (!registered) return
        runCatching { context.applicationContext.unregisterReceiver(receiver) }
        registered = false
    }

    private const val TAG = "BondTracker"
}
