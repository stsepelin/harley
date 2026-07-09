package ee.zeppl.companion.ble

import android.Manifest
import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.os.IBinder
import android.util.Log
import androidx.compose.runtime.snapshotFlow
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import ee.zeppl.companion.MainActivity
import ee.zeppl.companion.R
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * Foreground service that keeps the BLE link alive while the screen is
 * off. Owns the [BleClient] instance and swaps itself into
 * [OutboundSink.send] for the duration — the NotificationListener and
 * MediaWatcher don't know or care that there's a radio now; they
 * still hand bytes to the sink the same way.
 *
 * `FOREGROUND_SERVICE_CONNECTED_DEVICE` is the right subtype on
 * Android 14+ for a phone-to-companion BLE link. The user sees a
 * persistent notification while we hold the link.
 */
class BleService : Service() {

    private var client:           BleClient?     = null
    private var previousSinkSend: ((ByteArray) -> Unit)? = null
    private var calCollector:     SpeedCalCollector? = null
    private var rideCollector:    RideCollector?     = null
    private var callTracker:      CallStateTracker?  = null
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)

    override fun onCreate() {
        super.onCreate()
        ensureChannel()
        startForeground(NOTIF_ID, buildNotification(), FG_TYPE)

        // Keep the foreground notification text in sync with the real
        // BLE state. Without this, the notification says "Connected"
        // from the moment the user taps Connect cluster, even while the
        // scan is still running (or has failed) — which led to a
        // confusing demo where the system tray claimed connectivity that
        // didn't actually exist.
        scope.launch {
            snapshotFlow { BleState.conn to BleState.deviceName }
                .distinctUntilChanged()
                .collect { refreshNotification() }
        }

        val handler = CommandHandler(applicationContext)
        // Swap the global sink so any future notif / media event goes straight
        // to GATT instead of the default logcat path. Remember the prior lambda
        // so we can restore it in onDestroy. The actual connect is kicked off in
        // onStartCommand (which fires on every start, including while we're
        // already running - so Reconnect works even without a service restart).
        client = BleClient(applicationContext, onCommand = handler::dispatch).also { c ->
            previousSinkSend = OutboundSink.send
            OutboundSink.send = { bytes -> c.write(bytes) }
        }

        // Speed-calibration GPS sampling runs here (not in the wizard Composable)
        // so it keeps collecting while the ride screen is off. Register GPS only
        // while a run is active; sample the GPS/raw-count pair once a second.
        val collector = SpeedCalCollector(applicationContext).also { calCollector = it }
        scope.launch {
            snapshotFlow { CalibrationSession.active }
                .distinctUntilChanged()
                .collect { active ->
                    updateForegroundType(active)
                    collector.setActive(active)
                }
        }
        // Log rides from the telemetry stream (movement-gated; persisted to the
        // History tab). Shares the once-a-second cadence with the calibrator.
        val rides = RideCollector(applicationContext).also { rideCollector = it }
        scope.launch {
            while (isActive) {
                delay(1000)
                collector.tick()
                rides.tick(System.currentTimeMillis())
            }
        }
        // Finalize the in-progress ride the moment the link drops, since
        // TelemetryState is cleared on disconnect and the tick loop goes quiet.
        scope.launch {
            snapshotFlow { BleState.conn == BleConnState.CONNECTED }
                .distinctUntilChanged()
                .collect { connected -> if (!connected) rides.onLinkDown(System.currentTimeMillis()) }
        }
        // Mirror the phone's own call state to the cluster (answer/hang up on
        // the phone, not just via the cluster buttons).
        callTracker = CallStateTracker(applicationContext).also { it.start() }
        Log.i(TAG, "service started")
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val c = client
        if (c != null && intent != null) {
            val address = intent.getStringExtra(EXTRA_ADDRESS)
            val autoConnect = intent.getBooleanExtra(EXTRA_AUTOCONNECT, false)
            if (address != null) {
                c.connectAddress(address, autoConnect)
            } else {
                // No specific target: scan-and-take-first-match (fresh setup).
                c.start()
            }
        }
        // STICKY so Android restarts us after a low-memory kill once pressure
        // subsides. The user's connect intent is implicit: they tapped at some point.
        return START_STICKY
    }

    override fun onDestroy() {
        Log.i(TAG, "service stopping")
        scope.cancel()
        rideCollector?.onLinkDown(System.currentTimeMillis())
        rideCollector = null
        calCollector?.stop()
        calCollector = null
        callTracker?.stop()
        callTracker = null
        client?.stop()
        client = null
        previousSinkSend?.let { OutboundSink.send = it }
        previousSinkSend = null
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    // --- notification scaffolding ---------------------------------------

    private fun ensureChannel() {
        val nm = getSystemService(NotificationManager::class.java)
        if (nm.getNotificationChannel(CHANNEL_ID) == null) {
            nm.createNotificationChannel(
                NotificationChannel(
                    CHANNEL_ID,
                    getString(R.string.ble_channel_name),
                    // LOW: the user already chose to start us. No need
                    // for sound or vibration when the link comes up.
                    NotificationManager.IMPORTANCE_LOW,
                )
            )
        }
    }

    private fun buildNotification(): Notification {
        val tap = android.app.PendingIntent.getActivity(
            this, 0,
            Intent(this, MainActivity::class.java),
            android.app.PendingIntent.FLAG_IMMUTABLE,
        )
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle(getString(R.string.app_name))
            .setContentText(currentStatusText())
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentIntent(tap)
            .setOngoing(true)
            .build()
    }

    private fun currentStatusText(): String = when (BleState.conn) {
        BleConnState.IDLE         -> getString(R.string.ble_state_idle)
        BleConnState.SCANNING     -> getString(R.string.ble_state_scanning)
        BleConnState.CONNECTING   -> getString(R.string.ble_state_connecting, BleState.deviceName ?: "…")
        BleConnState.PAIRING      -> getString(R.string.ble_state_pairing,    BleState.deviceName ?: "cluster")
        BleConnState.CONNECTED    -> getString(R.string.ble_state_connected,  BleState.deviceName ?: "?")
        BleConnState.DISCONNECTED -> getString(R.string.ble_state_disconnected)
        BleConnState.WAITING      -> getString(R.string.ble_state_waiting,    BleState.deviceName ?: "cluster")
    }

    // NotificationManagerCompat handles the POST_NOTIFICATIONS gate
    // gracefully on Android 13+; the permission is requested as part of
    // the BLE permission bundle on first run.
    @SuppressLint("MissingPermission")
    private fun refreshNotification() {
        NotificationManagerCompat.from(this).notify(NOTIF_ID, buildNotification())
    }

    // While a calibration runs we add the LOCATION foreground-service type so
    // GPS keeps flowing with the screen off. Only add it when fine-location is
    // granted (the wizard requests it before starting) - startForeground with an
    // ungranted location type throws.
    @SuppressLint("MissingPermission")
    private fun updateForegroundType(calibrating: Boolean) {
        val withLocation = calibrating && ContextCompat.checkSelfPermission(
            this, Manifest.permission.ACCESS_FINE_LOCATION,
        ) == PackageManager.PERMISSION_GRANTED
        val type = if (withLocation) FG_TYPE or ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION else FG_TYPE
        try {
            startForeground(NOTIF_ID, buildNotification(), type)
        } catch (e: Exception) {
            Log.w(TAG, "FGS type update failed: $e")
        }
    }

    companion object {
        private const val TAG        = "BleService"
        private const val CHANNEL_ID = "zeppl_ble"
        private const val NOTIF_ID   = 1
        // ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE — the
        // permission we declared in the manifest is the gate.
        private const val FG_TYPE    = ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE

        private const val EXTRA_ADDRESS     = "zeppl.address"
        private const val EXTRA_AUTOCONNECT = "zeppl.autoConnect"

        /**
         * Start (or, if already running, redirect) the link. `address` = null
         * scans for the first cluster (fresh setup); a specific MAC connects
         * directly. `autoConnect=true` is for reconnecting to a bonded cluster
         * that may not be visible - it parks the address on the accept list.
         */
        fun start(context: Context, address: String? = null, autoConnect: Boolean = false) {
            val intent = Intent(context, BleService::class.java).apply {
                if (address != null) putExtra(EXTRA_ADDRESS, address)
                putExtra(EXTRA_AUTOCONNECT, autoConnect)
            }
            context.startForegroundService(intent)
        }

        fun stop(context: Context) {
            context.stopService(Intent(context, BleService::class.java))
        }
    }
}
