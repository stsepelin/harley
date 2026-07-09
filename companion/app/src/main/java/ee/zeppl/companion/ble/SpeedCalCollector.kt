package ee.zeppl.companion.ble

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Looper
import androidx.core.content.ContextCompat

private const val MS_TO_MPH = 2.2369362920544

/**
 * GPS speed sampler for the calibration wizard, hosted by [BleService] so it
 * keeps collecting with the screen off / app backgrounded. Registers GPS
 * updates only while [CalibrationSession.active], writes the speed into the
 * session, and on each [tick] pairs the latest GPS speed with the latest raw
 * ECM count ([TelemetryState.speedRaw]) into a calibration sample.
 */
class SpeedCalCollector(context: Context) {

    private val app = context.applicationContext
    private val lm = app.getSystemService(Context.LOCATION_SERVICE) as LocationManager
    private val listener = object : LocationListener {
        override fun onLocationChanged(location: Location) {
            if (location.hasSpeed()) CalibrationSession.gpsMph = location.speed * MS_TO_MPH
        }
    }
    private var registered = false

    @SuppressLint("MissingPermission")
    fun setActive(active: Boolean) {
        val granted = ContextCompat.checkSelfPermission(
            app, Manifest.permission.ACCESS_FINE_LOCATION,
        ) == PackageManager.PERMISSION_GRANTED
        if (active && granted && !registered) {
            try {
                lm.requestLocationUpdates(
                    LocationManager.GPS_PROVIDER, 1000L, 0f, listener, Looper.getMainLooper(),
                )
                registered = true
            } catch (_: SecurityException) {
            }
        } else if (!active && registered) {
            lm.removeUpdates(listener)
            registered = false
        }
    }

    /** Fold one paired reading if we have both a GPS fix and a raw count. */
    fun tick() {
        if (!CalibrationSession.active) return
        val mph = CalibrationSession.gpsMph ?: return
        val raw = TelemetryState.speedRaw ?: return
        CalibrationSession.addSample(raw, mph)
    }

    fun stop() = setActive(false)
}
