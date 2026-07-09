package com.vrodcluster.companion.ble

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
    var odometerM:      Long? by mutableStateOf(null)
    var trip1M:         Long? by mutableStateOf(null)
    var trip2M:         Long? by mutableStateOf(null)
    var trip1FuelTicks: Long? by mutableStateOf(null)
    var trip2FuelTicks: Long? by mutableStateOf(null)

    /** Uptime-millis of the last frame, or null if none received this session. */
    var lastFrameMs:    Long? by mutableStateOf(null)

    fun clear() {
        speedMph = null; speedRaw = null; rpm = null; gear = null
        engineTempC = null; fuelLevel = null
        odometerM = null; trip1M = null; trip2M = null
        trip1FuelTicks = null; trip2FuelTicks = null
        lastFrameMs = null
    }
}
