package ee.zeppl.companion.ride

import ee.zeppl.companion.ride.RideRecorder.Companion.IDLE_TIMEOUT_MS
import ee.zeppl.companion.ride.RideRecorder.Companion.MIN_RIDE_DISTANCE_M
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class RideRecorderTest {

    private val sec = 1000L

    @Test fun `stationary samples never start a ride`() {
        val r = RideRecorder()
        assertNull(r.sample(0, 0, 1000, 0))
        assertNull(r.sample(sec, 0, 1000, 0))
        assertNull(r.finish(2 * sec))
    }

    @Test fun `a moving ride finalizes on idle timeout with distance and speeds`() {
        val r = RideRecorder()
        var t = 0L
        assertNull(r.sample(t, 30, 1000, 100)); t += sec       // start, max=30
        assertNull(r.sample(t, 55, 1400, 140)); t += sec       // +400 m, max=55
        assertNull(r.sample(t, 20, 1600, 160)); t += sec       // +200 m, now 600 m total
        // Bike stops; nothing fires until the idle window elapses.
        assertNull(r.sample(t, 0, 1600, 160))
        val ride = r.sample(t + IDLE_TIMEOUT_MS, 0, 1600, 160)!!
        assertEquals(600L, ride.distanceMeters)                 // 1600 - 1000 odo delta
        assertEquals(55, ride.maxSpeedMph)
        assertEquals(60, ride.fuelTicks)                        // 160 - 100
        assertEquals(0L, ride.startEpochMs)
        // Ends when it last moved (the 20 mph sample at t=2s), not across the idle gap.
        assertEquals(2 * sec, ride.endEpochMs)
        // 3 samples were moving-to-moving intervals: only the first two gaps counted.
        assertEquals(2 * sec, ride.movingMs)
    }

    @Test fun `a trivial short ride is discarded`() {
        val r = RideRecorder()
        assertNull(r.sample(0, 5, 1000, 0))
        assertNull(r.sample(sec, 5, 1000 + MIN_RIDE_DISTANCE_M - 1, 0))  // just under threshold
        assertNull(r.finish(2 * sec))                                     // dropped
    }

    @Test fun `disconnect finalizes an in-progress ride at last movement`() {
        val r = RideRecorder()
        assertNull(r.sample(0, 40, 0, 0))
        assertNull(r.sample(sec, 40, 500, 0))
        val ride = r.finish(sec + 30_000)!!    // link drops 30 s later
        assertEquals(500L, ride.distanceMeters)
        assertEquals(sec, ride.endEpochMs)     // stamped at last motion, not the drop
    }

    @Test fun `a trip counter reset mid-ride zeroes fuel`() {
        val r = RideRecorder()
        assertNull(r.sample(0, 40, 0, 500))
        assertNull(r.sample(sec, 40, 600, 20))   // ticks dropped below start -> reset
        val ride = r.finish(2 * sec)!!
        assertEquals(0L, ride.fuelTicks)
    }

    @Test fun `null fuel ticks yield zero and no crash`() {
        val r = RideRecorder()
        assertNull(r.sample(0, 40, 0, null))
        assertNull(r.sample(sec, 40, 600, null))
        val ride = r.finish(2 * sec)!!
        assertEquals(0L, ride.fuelTicks)
    }

    @Test fun `re-moving after a finalized ride starts a fresh one`() {
        val r = RideRecorder()
        assertNull(r.sample(0, 40, 0, 0))
        assertNull(r.sample(sec, 40, 500, 0))
        assertNull(r.sample(2 * sec, 0, 500, 0))
        val first = r.sample(2 * sec + IDLE_TIMEOUT_MS, 0, 500, 0)!!
        assertEquals(500L, first.distanceMeters)

        // A new leg after the timeout is its own ride.
        val t = 2 * sec + IDLE_TIMEOUT_MS + sec
        assertNull(r.sample(t, 30, 500, 0))
        assertNull(r.sample(t + sec, 30, 900, 0))
        val second = r.finish(t + 2 * sec)!!
        assertEquals(400L, second.distanceMeters)
        assertTrue(second.startEpochMs > first.endEpochMs)
    }

    @Test fun `finish with no active ride returns null`() {
        assertNull(RideRecorder().finish(1234))
    }

    @Test fun `odometer going backwards does not produce negative distance`() {
        val r = RideRecorder()
        assertNull(r.sample(0, 40, 5000, 0))
        assertNull(r.sample(sec, 40, 4000, 0))   // bogus backwards reading
        assertNull(r.finish(2 * sec))            // distance clamps to 0 -> discarded
    }
}
