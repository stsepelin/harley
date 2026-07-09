package ee.zeppl.companion.cal

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.roundToInt

class SpeedCalibratorTest {

    @Test fun `fits the exact divisor from clean samples`() {
        val d = 180
        val samples = listOf(20.0, 30.0, 45.0, 60.0, 75.0).map {
            SpeedCalibrator.Sample((d * it).roundToInt(), it)
        }
        val r = SpeedCalibrator.compute(samples)!!
        assertEquals(180, r.divisor)
        assertEquals(5, r.sampleCount)
        assertTrue("rms should be tiny for clean data", r.rmsErrorMph < 0.5)
    }

    @Test fun `too few usable samples returns null`() {
        assertNull(SpeedCalibrator.compute(listOf(SpeedCalibrator.Sample(3600, 20.0))))
    }

    @Test fun `filters low-speed and zero-raw noise`() {
        val samples = listOf(
            SpeedCalibrator.Sample(900, 5.0),    // below the speed floor
            SpeedCalibrator.Sample(0, 20.0),     // zero raw
            SpeedCalibrator.Sample(3600, 20.0),  // the only usable one
        )
        assertNull(SpeedCalibrator.compute(samples))
    }

    @Test fun `clamps an out-of-range divisor to the firmware bounds`() {
        // Absurd ratio (1000 counts/mph) must clamp, not push a rejected value.
        val samples = List(6) { SpeedCalibrator.Sample(20000, 20.0) }
        val r = SpeedCalibrator.compute(samples)!!
        assertEquals(SpeedCalibrator.DIVISOR_MAX, r.divisor)
    }
}
