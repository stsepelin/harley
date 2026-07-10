package ee.zeppl.companion.ble

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * Cluster -> phone telemetry decoder. The 37-byte fixture below is the exact
 * frame asserted by the firmware host test
 * (`test_telemetry_codec.c::test_encode_exact_frame`) - if either side's
 * layout moves, both fixtures move together.
 */
class TelemetryCodecTest {

    // Byte-for-byte the expected[] from test_encode_exact_frame.
    private val fixture = intArrayOf(
        0x40, 0x22, 0x00,             // type, payload_len = 34
        0x00, 0x30,                   // speed_raw  = 12288
        0x3F, 0x00,                   // speed_mph  = 63
        0x80, 0x0C,                   // rpm        = 3200
        0x03,                         // gear       = 3
        0xF9,                         // engine_temp_c = -7
        0x05,                         // fuel_level = 5
        0x14, 0x00,                   // lamps      = low_beam | neutral
        0x40, 0x42, 0x0F, 0x00,       // odometer_m = 1000000
        0x39, 0x30, 0x00, 0x00,       // trip1_m    = 12345
        0x31, 0xD4, 0x00, 0x00,       // trip2_m    = 54321
        0x57, 0x04, 0x00, 0x00,       // trip1_fuel = 1111
        0xAE, 0x08, 0x00, 0x00,       // trip2_fuel = 2222
        0x08, 0x18,                   // clock 08:24
        0x06,                         // status = LAYOUT_MAP | MAP_AVAILABLE
    ).map { it.toByte() }.toByteArray()

    @Test fun `decodes the firmware fixture frame`() {
        val f = TelemetryCodec.decode(fixture)!!
        assertEquals(12288, f.speedRaw)
        assertEquals(63, f.speedMph)
        assertEquals(3200, f.rpm)
        assertEquals(3, f.gear)
        assertEquals(-7, f.engineTempC)
        assertEquals(5, f.fuelLevel)
        assertEquals(TelemetryCodec.LAMP_LOW_BEAM or TelemetryCodec.LAMP_NEUTRAL, f.lamps)
        assertEquals(1000000L, f.odometerM)
        assertEquals(12345L, f.trip1M)
        assertEquals(54321L, f.trip2M)
        assertEquals(1111L, f.trip1FuelTicks)
        assertEquals(2222L, f.trip2FuelTicks)
        assertEquals(8, f.clockHours)
        assertEquals(24, f.clockMinutes)
        assertEquals(
            TelemetryCodec.STATUS_LAYOUT_MAP or TelemetryCodec.STATUS_MAP_AVAILABLE,
            f.status,
        )
    }

    @Test fun `rejects a short buffer`() {
        assertNull(TelemetryCodec.decode(fixture.copyOf(TelemetryCodec.FRAME_LEN - 1)))
    }

    @Test fun `rejects a wrong type byte`() {
        val bad = fixture.copyOf()
        bad[0] = 0x30  // a command type, not telemetry
        assertNull(TelemetryCodec.decode(bad))
    }

    @Test fun `rejects a wrong payload length`() {
        val bad = fixture.copyOf()
        bad[1] = 0x21  // 33, not 34
        assertNull(TelemetryCodec.decode(bad))
    }

    @Test fun `applies a frame into TelemetryState`() {
        TelemetryState.clear()
        TelemetryState.apply(TelemetryCodec.decode(fixture)!!, atMs = 4242L)
        assertEquals(63, TelemetryState.speedMph)
        assertEquals(12288, TelemetryState.speedRaw)
        assertEquals(1000000L, TelemetryState.odometerM)
        assertEquals(4242L, TelemetryState.lastFrameMs)
        assertEquals(true, TelemetryState.layoutIsMap)   // status bit 1
        assertEquals(true, TelemetryState.mapAvailable)  // status bit 2
        assertEquals(false, TelemetryState.mapSupported) // status bit 0 clear in the fixture
        TelemetryState.clear()
        assertNull(TelemetryState.speedMph)
        assertNull(TelemetryState.layoutIsMap)
    }
}
