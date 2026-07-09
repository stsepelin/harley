package ee.zeppl.companion.ble

import android.content.Context
import ee.zeppl.companion.ride.RideRecorder
import ee.zeppl.companion.ride.RideStore

/**
 * Bridges live telemetry to the ride log, hosted by [BleService] so it keeps
 * recording with the screen off. On each [tick] it feeds the latest
 * [TelemetryState] into a pure [RideRecorder]; a completed ride (idle timeout,
 * or [onLinkDown] when the cluster disconnects) is persisted to [RideStore].
 * Fuel uses trip1's injector ticks - best-effort, the recorder guards against a
 * mid-ride trip reset.
 */
class RideCollector(context: Context) {

    private val app = context.applicationContext
    private val recorder = RideRecorder()

    init {
        RideStore.ensureLoaded(app)
    }

    /** Fold one sample from the current telemetry snapshot. `nowMs` = wall clock. */
    fun tick(nowMs: Long) {
        val speed = TelemetryState.speedMph ?: return
        val odo = TelemetryState.odometerM ?: return
        recorder.sample(nowMs, speed, odo, TelemetryState.trip1FuelTicks)
            ?.let { RideStore.add(app, it) }
    }

    /** The link dropped (or the service is stopping): finalize any ride in progress. */
    fun onLinkDown(nowMs: Long) {
        recorder.finish(nowMs)?.let { RideStore.add(app, it) }
    }
}
