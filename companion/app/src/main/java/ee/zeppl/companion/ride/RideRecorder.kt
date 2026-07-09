package ee.zeppl.companion.ride

import kotlinx.serialization.Serializable

/**
 * One completed ride. Distance comes from the cluster's total odometer delta
 * (monotonic, the most trustworthy source); `movingMs` is the time spent
 * actually rolling, so avg speed excludes stops. `fuelTicks` is the raw
 * injector-tick delta over the ride (0 = unknown); economy is derived at
 * display time from the current mL/tick calibration, so recalibrating fixes
 * history retroactively.
 */
@Serializable
data class RideRecord(
    val startEpochMs: Long,
    val endEpochMs: Long,
    val distanceMeters: Long,
    val movingMs: Long,
    val maxSpeedMph: Int,
    val fuelTicks: Long,
)

/**
 * Pure state machine that turns a stream of telemetry samples into discrete
 * [RideRecord]s. Movement-gated: a ride begins on the first sample at or above
 * [START_SPEED_MPH] and ends when the bike has been stationary for
 * [IDLE_TIMEOUT_MS] (a fuel stop or the end of the trip) or when the link
 * drops ([finish]). Trivial rides shorter than [MIN_RIDE_DISTANCE_M] are
 * discarded, so rolling the bike in the garage or a brief connect test doesn't
 * litter the history.
 *
 * No Android or clock dependency: the caller passes a wall-clock millis as
 * `nowMs` (which also becomes the record's timestamp), so this unit-tests
 * directly. Hosted by [ee.zeppl.companion.ble.RideCollector].
 */
class RideRecorder {

    private var active = false
    private var startMs = 0L
    private var startOdometerM = 0L
    private var startFuelTicks = 0L

    private var lastMs = 0L
    private var lastMoveMs = 0L
    private var lastOdometerM = 0L
    private var lastFuelTicks = 0L
    private var maxSpeedMph = 0
    private var movingMs = 0L
    private var fuelReset = false

    /**
     * Fold one telemetry sample. `fuelTicks` is the per-trip injector counter
     * (nullable when the cluster hasn't sent one). Returns a finished
     * [RideRecord] when this sample ends a ride via idle timeout, else null.
     */
    fun sample(nowMs: Long, speedMph: Int, odometerM: Long, fuelTicks: Long?): RideRecord? {
        if (!active) {
            if (speedMph >= START_SPEED_MPH) begin(nowMs, speedMph, odometerM, fuelTicks)
            return null
        }

        val dt = (nowMs - lastMs).coerceAtLeast(0)
        if (speedMph >= START_SPEED_MPH) movingMs += dt
        if (speedMph > maxSpeedMph) maxSpeedMph = speedMph
        if (odometerM >= lastOdometerM) lastOdometerM = odometerM
        if (fuelTicks != null) {
            if (fuelTicks < startFuelTicks) fuelReset = true
            lastFuelTicks = fuelTicks
        }
        lastMs = nowMs

        if (speedMph >= START_SPEED_MPH) {
            lastMoveMs = nowMs
            return null
        }
        // Stationary: end the ride once we've been still long enough. The ride
        // effectively ended when it last moved, so stamp the end there rather
        // than dragging the duration out across the idle window.
        if (nowMs - lastMoveMs >= IDLE_TIMEOUT_MS) return finalize(lastMoveMs)
        return null
    }

    /**
     * Force-finish an in-progress ride (the link dropped or the service is
     * stopping). Ends the ride at the last moment it was moving. Returns the
     * record if one was in progress and survives the trivial-ride filter.
     */
    fun finish(nowMs: Long): RideRecord? {
        if (!active) return null
        val endMs = if (lastMoveMs in (startMs + 1)..nowMs) lastMoveMs else nowMs
        return finalize(endMs)
    }

    private fun begin(nowMs: Long, speedMph: Int, odometerM: Long, fuelTicks: Long?) {
        active = true
        startMs = nowMs
        startOdometerM = odometerM
        startFuelTicks = fuelTicks ?: 0L
        lastMs = nowMs
        lastMoveMs = nowMs
        lastOdometerM = odometerM
        lastFuelTicks = fuelTicks ?: 0L
        maxSpeedMph = speedMph
        movingMs = 0L
        fuelReset = false
    }

    private fun finalize(endMs: Long): RideRecord? {
        active = false
        val distance = (lastOdometerM - startOdometerM).coerceAtLeast(0)
        if (distance < MIN_RIDE_DISTANCE_M) return null
        val ticks = if (!fuelReset && lastFuelTicks >= startFuelTicks) {
            lastFuelTicks - startFuelTicks
        } else {
            0L
        }
        return RideRecord(
            startEpochMs = startMs,
            endEpochMs = endMs.coerceAtLeast(startMs),
            distanceMeters = distance,
            movingMs = movingMs,
            maxSpeedMph = maxSpeedMph,
            fuelTicks = ticks,
        )
    }

    companion object {
        /** Speed at or above which the bike counts as moving (mph). */
        const val START_SPEED_MPH = 1

        /** Stationary this long ends the ride (5 min: covers long lights, splits fuel stops). */
        const val IDLE_TIMEOUT_MS = 5 * 60_000L

        /** Rides shorter than this are dropped as noise (garage shuffles, connect tests). */
        const val MIN_RIDE_DISTANCE_M = 200L
    }
}
