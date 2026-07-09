package ee.zeppl.companion.cal

import kotlin.math.roundToInt
import kotlin.math.sqrt

/**
 * Solves the cluster's speed divisor from paired (raw ECM count, phone GPS
 * speed) samples collected while riding. The relationship is linear through the
 * origin - raw = divisor * mph - so the best-fit divisor is the least-squares
 * slope, Sum(raw*mph) / Sum(mph*mph). Pure and unit-tested; the Android GPS
 * plumbing lives in the wizard.
 */
object SpeedCalibrator {

    /** One paired reading. `speedRaw` is the cluster's raw count; `gpsMph` is truth. */
    data class Sample(val speedRaw: Int, val gpsMph: Double)

    data class Result(
        val divisor: Int,       // clamped to the firmware's accepted range
        val sampleCount: Int,   // samples that actually fed the fit
        val rmsErrorMph: Double, // fit quality: RMS of (fitted mph - GPS mph)
    )

    // Mirror the firmware bounds (settings.h) so we never push a value it rejects.
    const val DIVISOR_MIN = 50
    const val DIVISOR_MAX = 400

    // GPS speed is noisy near standstill and the ratio blows up, so only fit
    // samples above a floor; require a handful for a trustworthy slope.
    const val MIN_SPEED_MPH = 10.0
    const val MIN_SAMPLES = 5

    /** Returns the fitted result, or null if there aren't enough usable samples. */
    fun compute(samples: List<Sample>): Result? {
        val used = samples.filter { it.gpsMph >= MIN_SPEED_MPH && it.speedRaw > 0 }
        if (used.size < MIN_SAMPLES) return null

        var num = 0.0
        var den = 0.0
        for (s in used) {
            num += s.speedRaw * s.gpsMph
            den += s.gpsMph * s.gpsMph
        }
        if (den <= 0.0) return null

        val rawDivisor = num / den
        val divisor = rawDivisor.roundToInt().coerceIn(DIVISOR_MIN, DIVISOR_MAX)

        var sse = 0.0
        for (s in used) {
            val fittedMph = s.speedRaw / divisor.toDouble()
            val e = fittedMph - s.gpsMph
            sse += e * e
        }
        return Result(divisor, used.size, sqrt(sse / used.size))
    }
}
