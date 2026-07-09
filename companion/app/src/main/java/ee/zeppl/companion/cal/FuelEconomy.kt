package ee.zeppl.companion.cal

/**
 * Fuel-economy + range math (Brick 4). The cluster streams a per-trip fuel
 * counter in raw injector "ticks" (trip1FuelTicks) alongside trip distance in
 * metres; one fill-up calibrates how much fuel a tick represents
 * (mL/tick), after which any (distance, ticks) window yields economy, and the
 * level sender plus economy give range-to-empty.
 *
 * Pure and unit-tested; the Android persistence + UI live alongside.
 */
object FuelEconomy {

    /** V-Rod VRSCF tank, litres (5.0 US gal). */
    const val TANK_LITERS = 18.9

    /** The cluster's fuel-level sender is a coarse 0..6 bar reading. */
    const val FUEL_LEVEL_MAX = 6

    private const val METERS_PER_MILE = 1609.344
    private const val LITERS_PER_US_GAL = 3.785411784

    /**
     * Solve mL-per-tick from one fill-up: `litersAdded` topped the tank after
     * `ticks` were counted since the last fill. Null if either is non-positive.
     */
    fun calibrateMlPerTick(litersAdded: Double, ticks: Long): Double? {
        if (litersAdded <= 0.0 || ticks <= 0) return null
        return litersAdded * 1000.0 / ticks
    }

    fun litersUsed(ticks: Long, mlPerTick: Double): Double = ticks * mlPerTick / 1000.0

    data class Economy(
        val kmPerLiter: Double,
        val litersPer100km: Double,
        val mpgUs: Double,
    )

    /** Economy over a (distance, ticks) window, or null if any input is unusable. */
    fun economy(distanceMeters: Long, ticks: Long, mlPerTick: Double): Economy? {
        if (distanceMeters <= 0 || ticks <= 0 || mlPerTick <= 0.0) return null
        val liters = litersUsed(ticks, mlPerTick)
        if (liters <= 0.0) return null
        val km = distanceMeters / 1000.0
        val miles = distanceMeters / METERS_PER_MILE
        val gallons = liters / LITERS_PER_US_GAL
        return Economy(
            kmPerLiter = km / liters,
            litersPer100km = liters / km * 100.0,
            mpgUs = miles / gallons,
        )
    }

    /** Coarse litres remaining from the 0..6 level sender (linear approximation). */
    fun litersFromLevel(level: Int): Double =
        level.coerceIn(0, FUEL_LEVEL_MAX).toDouble() / FUEL_LEVEL_MAX * TANK_LITERS

    /** Distance the remaining fuel buys at the given economy. Zero if either is unusable. */
    fun rangeKm(litersRemaining: Double, kmPerLiter: Double): Double {
        if (litersRemaining <= 0.0 || kmPerLiter <= 0.0) return 0.0
        return litersRemaining * kmPerLiter
    }

    fun rangeMiles(litersRemaining: Double, kmPerLiter: Double): Double =
        rangeKm(litersRemaining, kmPerLiter) * 1000.0 / METERS_PER_MILE
}
