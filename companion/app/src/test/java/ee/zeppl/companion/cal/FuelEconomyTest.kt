package ee.zeppl.companion.cal

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class FuelEconomyTest {

    @Test fun `calibrates ml per tick from a fill-up`() {
        // 18.9 L over 37800 ticks -> 0.5 mL/tick.
        assertEquals(0.5, FuelEconomy.calibrateMlPerTick(18.9, 37800)!!, 1e-9)
    }

    @Test fun `calibration rejects non-positive inputs`() {
        assertNull(FuelEconomy.calibrateMlPerTick(0.0, 100))
        assertNull(FuelEconomy.calibrateMlPerTick(10.0, 0))
    }

    @Test fun `computes economy over a window`() {
        // 100 km on 5.0 L (10000 ticks at 0.5 mL/tick).
        val e = FuelEconomy.economy(distanceMeters = 100_000, ticks = 10_000, mlPerTick = 0.5)!!
        assertEquals(20.0, e.kmPerLiter, 1e-6)
        assertEquals(5.0, e.litersPer100km, 1e-6)
        assertEquals(47.04, e.mpgUs, 0.01)
    }

    @Test fun `economy rejects unusable inputs`() {
        assertNull(FuelEconomy.economy(0, 10_000, 0.5))
        assertNull(FuelEconomy.economy(100_000, 0, 0.5))
        assertNull(FuelEconomy.economy(100_000, 10_000, 0.0))
    }

    @Test fun `maps the level sender to litres, clamped`() {
        assertEquals(0.0, FuelEconomy.litersFromLevel(0), 1e-9)
        assertEquals(9.45, FuelEconomy.litersFromLevel(3), 1e-9)
        assertEquals(FuelEconomy.TANK_LITERS, FuelEconomy.litersFromLevel(6), 1e-9)
        assertEquals(FuelEconomy.TANK_LITERS, FuelEconomy.litersFromLevel(99), 1e-9)
    }

    @Test fun `range is litres times economy`() {
        assertEquals(189.0, FuelEconomy.rangeKm(9.45, 20.0), 1e-6)
        assertEquals(0.0, FuelEconomy.rangeKm(0.0, 20.0), 1e-9)
        assertEquals(0.0, FuelEconomy.rangeKm(9.45, 0.0), 1e-9)
        // 189 km -> ~117.4 mi.
        assertEquals(117.44, FuelEconomy.rangeMiles(9.45, 20.0), 0.01)
    }
}
