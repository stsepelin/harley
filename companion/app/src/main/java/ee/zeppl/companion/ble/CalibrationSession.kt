package ee.zeppl.companion.ble

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import ee.zeppl.companion.cal.SpeedCalibrator

/**
 * Live state of a speed-calibration run, shared between the wizard UI and the
 * [SpeedCalCollector] that the foreground [BleService] drives. Kept here (not in
 * the Composable) so sample collection survives screen-off / backgrounding -
 * the earlier Composable-scoped collector stopped the moment the ride screen
 * slept, so the sample count never reached the threshold and Finish never
 * enabled.
 *
 * The UI observes these fields and starts/stops the run; the service-side
 * collector writes GPS speed + folds in samples.
 */
object CalibrationSession {

    /** True while the collector should be sampling. Toggled by the UI. */
    var active by mutableStateOf(false)
        private set

    /** Latest GPS speed (mph), written by the collector; null until first fix. */
    var gpsMph by mutableStateOf<Double?>(null)

    val samples = mutableStateListOf<SpeedCalibrator.Sample>()

    /** Best-fit result so far, or null until enough samples land. */
    var result by mutableStateOf<SpeedCalibrator.Result?>(null)
        private set

    fun start() {
        samples.clear()
        result = null
        gpsMph = null
        active = true
    }

    /** Stop sampling but keep samples + result (for the review/apply step). */
    fun stop() {
        active = false
    }

    fun reset() {
        active = false
        samples.clear()
        result = null
        gpsMph = null
    }

    /** Fold one paired reading in and re-fit. Called ~1/s by the collector. */
    fun addSample(speedRaw: Int, gps: Double) {
        samples.add(SpeedCalibrator.Sample(speedRaw, gps))
        result = SpeedCalibrator.compute(samples.toList())
    }
}
