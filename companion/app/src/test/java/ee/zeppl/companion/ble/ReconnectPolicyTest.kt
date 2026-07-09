package ee.zeppl.companion.ble

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ReconnectPolicyTest {

    @Test
    fun `supervision timeout re-arms - the ignition-cycle case`() {
        assertTrue(ReconnectPolicy.shouldReconnect(ReconnectPolicy.STATUS_SUPERVISION_TIMEOUT))
    }

    @Test
    fun `lmp timeout and failed-to-establish keep waiting`() {
        assertTrue(ReconnectPolicy.shouldReconnect(ReconnectPolicy.STATUS_LMP_TIMEOUT))
        assertTrue(ReconnectPolicy.shouldReconnect(ReconnectPolicy.STATUS_FAILED_TO_ESTABLISH))
    }

    @Test
    fun `deliberate disconnects do not re-arm`() {
        // Cluster's "TAP TO DISCONNECT" row terminates from the remote side.
        assertFalse(ReconnectPolicy.shouldReconnect(ReconnectPolicy.STATUS_REMOTE_TERMINATED))
        // Our own Disconnect button terminates locally.
        assertFalse(ReconnectPolicy.shouldReconnect(ReconnectPolicy.STATUS_LOCAL_TERMINATED))
        // Clean local close reports success.
        assertFalse(ReconnectPolicy.shouldReconnect(0))
    }

    @Test
    fun `unknown statuses default to not reconnecting`() {
        assertFalse(ReconnectPolicy.shouldReconnect(133))   // the classic GATT_ERROR
        assertFalse(ReconnectPolicy.shouldReconnect(-1))
    }
}
