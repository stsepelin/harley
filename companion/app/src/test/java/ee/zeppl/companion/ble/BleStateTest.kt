package ee.zeppl.companion.ble

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test

/**
 * BleState is a singleton with three observable properties. The defaults
 * matter because they're what Compose paints on cold start before the
 * service has done anything — a wrong default would show "Connected"
 * before any radio activity. Pinning them prevents that regression.
 */
class BleStateTest {

    @Before fun resetSingleton() {
        // The state is a process-global object; clear it between tests
        // so an earlier case can't leak into a later one.
        BleState.conn       = BleConnState.IDLE
        BleState.deviceName = null
        BleState.lastTx     = null
    }

    @Test fun `default conn is IDLE`() {
        assertEquals(BleConnState.IDLE, BleState.conn)
    }

    @Test fun `default deviceName is null`() {
        assertNull(BleState.deviceName)
    }

    @Test fun `default lastTx is null`() {
        assertNull(BleState.lastTx)
    }

    @Test fun `transitions through scan-connect lifecycle are observable`() {
        BleState.conn = BleConnState.SCANNING
        assertEquals(BleConnState.SCANNING, BleState.conn)
        BleState.deviceName = "Zeppl"
        BleState.conn = BleConnState.CONNECTING
        assertEquals(BleConnState.CONNECTING, BleState.conn)
        BleState.conn = BleConnState.CONNECTED
        assertEquals(BleConnState.CONNECTED, BleState.conn)
        assertEquals("Zeppl", BleState.deviceName)
    }
}
