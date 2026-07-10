package ee.zeppl.companion.ble

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * One decoded cluster telemetry frame. Mirrors `vehicle_data_t` fields that
 * the cluster streams. u16/u32 wire fields widen to Int/Long so their full
 * unsigned range survives; `engineTempC` stays signed.
 */
data class TelemetryFrame(
    val speedRaw: Int,
    val speedMph: Int,
    val rpm: Int,
    val gear: Int,
    val engineTempC: Int,
    val fuelLevel: Int,
    val lamps: Int,
    val odometerM: Long,
    val trip1M: Long,
    val trip2M: Long,
    val trip1FuelTicks: Long,
    val trip2FuelTicks: Long,
    val clockHours: Int,
    val clockMinutes: Int,
    val status: Int,
)

/**
 * Decoder for the cluster -> phone telemetry frame. Mirrors
 * `firmware/main/phone/telemetry_codec.c` byte-for-byte; the firmware host
 * test (`test_telemetry_codec.c`, test_encode_exact_frame) is the canonical
 * fixture that `TelemetryCodecTest.kt` cross-checks. Touch one, touch both.
 */
object TelemetryCodec {
    const val TYPE: Byte = 0x40
    private const val PAYLOAD_LEN = 34
    const val FRAME_LEN = 3 + PAYLOAD_LEN  // 37

    // status bitfield (mirror TELEMETRY_STATUS_* in telemetry_codec.h).
    const val STATUS_MAP_SUPPORTED = 1 shl 0
    const val STATUS_LAYOUT_MAP    = 1 shl 1
    const val STATUS_MAP_AVAILABLE = 1 shl 2

    // Lamp bitfield positions (mirror TELEMETRY_LAMP_* in telemetry_codec.h).
    const val LAMP_TURN_LEFT    = 1 shl 0
    const val LAMP_TURN_RIGHT   = 1 shl 1
    const val LAMP_LOW_BEAM     = 1 shl 2
    const val LAMP_HIGH_BEAM    = 1 shl 3
    const val LAMP_NEUTRAL      = 1 shl 4
    const val LAMP_OIL          = 1 shl 5
    const val LAMP_CHECK_ENGINE = 1 shl 6
    const val LAMP_ABS          = 1 shl 7
    const val LAMP_BATTERY      = 1 shl 8
    const val LAMP_IMMOBILISER  = 1 shl 9

    /** Decode one frame, or null on any framing mismatch (drop the packet). */
    fun decode(bytes: ByteArray): TelemetryFrame? {
        if (bytes.size < FRAME_LEN) return null
        if (bytes[0] != TYPE) return null
        val payloadLen = (bytes[1].toInt() and 0xFF) or ((bytes[2].toInt() and 0xFF) shl 8)
        if (payloadLen != PAYLOAD_LEN) return null

        val b = ByteBuffer.wrap(bytes, 3, PAYLOAD_LEN).order(ByteOrder.LITTLE_ENDIAN)
        return TelemetryFrame(
            speedRaw       = b.short.toInt() and 0xFFFF,
            speedMph       = b.short.toInt() and 0xFFFF,
            rpm            = b.short.toInt() and 0xFFFF,
            gear           = b.get().toInt() and 0xFF,
            engineTempC    = b.get().toInt(),                  // signed i8
            fuelLevel      = b.get().toInt() and 0xFF,
            lamps          = b.short.toInt() and 0xFFFF,
            odometerM      = b.int.toLong() and 0xFFFFFFFFL,
            trip1M         = b.int.toLong() and 0xFFFFFFFFL,
            trip2M         = b.int.toLong() and 0xFFFFFFFFL,
            trip1FuelTicks = b.int.toLong() and 0xFFFFFFFFL,
            trip2FuelTicks = b.int.toLong() and 0xFFFFFFFFL,
            clockHours     = b.get().toInt() and 0xFF,
            clockMinutes   = b.get().toInt() and 0xFF,
            status         = b.get().toInt() and 0xFF,
        )
    }
}
