package ee.zeppl.companion.ble

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/**
 * Latest decoded telemetry pushed from the cluster over the TX notify
 * characteristic. Observable singleton in the same spirit as [BleState]: the
 * Ride dashboard reads these fields and re-renders when they change.
 *
 * Every field is nullable and starts null - the dashboard renders a dash
 * placeholder until the first frame lands, and falls back to "last known"
 * (the retained values) when the link drops. The decoder that fills this
 * arrives with Brick 1 (cluster to phone telemetry); until then the shell
 * renders the empty/offline state.
 */
object TelemetryState {
    var speedMph:       Int? by mutableStateOf(null)
    var speedRaw:       Int? by mutableStateOf(null)   // raw ECM count, for GPS calibration
    var rpm:            Int? by mutableStateOf(null)
    var gear:           Int? by mutableStateOf(null)   // 0 = N, 1..6, 7 = unknown
    var engineTempC:    Int? by mutableStateOf(null)
    var fuelLevel:      Int? by mutableStateOf(null)   // 0..6
    var lamps:          Int? by mutableStateOf(null)   // bitfield, see TelemetryCodec.LAMP_*
    var odometerM:      Long? by mutableStateOf(null)
    var trip1M:         Long? by mutableStateOf(null)
    var trip2M:         Long? by mutableStateOf(null)
    var trip1FuelTicks: Long? by mutableStateOf(null)
    var trip2FuelTicks: Long? by mutableStateOf(null)

    // Cluster layout state (from the telemetry status byte) so the Layout
    // toggle reflects reality rather than an optimistic guess.
    var mapSupported:   Boolean? by mutableStateOf(null)  // a map build (else hide the toggle)
    var layoutIsMap:    Boolean? by mutableStateOf(null)  // current view is the map
    var mapAvailable:   Boolean? by mutableStateOf(null)  // map loaded OK (card/tiles present)

    /** Uptime-millis of the last frame, or null if none received this session. */
    var lastFrameMs:    Long? by mutableStateOf(null)

    /** Fold a decoded frame into the observable snapshot. `atMs` is the caller's
     *  clock (SystemClock.uptimeMillis) so this stays free of android deps. */
    fun apply(f: TelemetryFrame, atMs: Long) {
        speedMph = f.speedMph; speedRaw = f.speedRaw; rpm = f.rpm; gear = f.gear
        engineTempC = f.engineTempC; fuelLevel = f.fuelLevel; lamps = f.lamps
        odometerM = f.odometerM; trip1M = f.trip1M; trip2M = f.trip2M
        trip1FuelTicks = f.trip1FuelTicks; trip2FuelTicks = f.trip2FuelTicks
        mapSupported = (f.status and TelemetryCodec.STATUS_MAP_SUPPORTED) != 0
        layoutIsMap  = (f.status and TelemetryCodec.STATUS_LAYOUT_MAP) != 0
        mapAvailable = (f.status and TelemetryCodec.STATUS_MAP_AVAILABLE) != 0
        lastFrameMs = atMs
    }

    fun clear() {
        speedMph = null; speedRaw = null; rpm = null; gear = null
        engineTempC = null; fuelLevel = null; lamps = null
        odometerM = null; trip1M = null; trip2M = null
        trip1FuelTicks = null; trip2FuelTicks = null
        mapSupported = null; layoutIsMap = null; mapAvailable = null
        lastFrameMs = null
    }
}
