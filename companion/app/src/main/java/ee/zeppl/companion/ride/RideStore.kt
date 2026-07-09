package ee.zeppl.companion.ride

import android.content.Context
import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import kotlinx.serialization.json.Json
import java.io.File

/**
 * Durable ride history. Rides are few and append-only, so a single JSON file in
 * filesDir is enough - no Room/SQLite. The list is an observable Compose state
 * (newest-first) so the History screen recomposes when a ride lands; the
 * recorder ([ee.zeppl.companion.ble.RideCollector]) appends via [add].
 */
object RideStore {

    private const val FILE = "rides.json"
    private const val TAG = "RideStore"
    private val json = Json { ignoreUnknownKeys = true }

    var rides: List<RideRecord> by mutableStateOf(emptyList())
        private set

    private var loaded = false

    /** Read the file once per process. Safe to call from anywhere; no-ops after the first load. */
    fun ensureLoaded(context: Context) {
        if (loaded) return
        loaded = true
        rides = runCatching {
            val f = file(context)
            if (!f.exists()) emptyList()
            else json.decodeFromString<List<RideRecord>>(f.readText()).sortedByDescending { it.startEpochMs }
        }.onFailure { Log.w(TAG, "load failed: $it") }.getOrDefault(emptyList())
    }

    /** Append a completed ride and persist. Newest sorts to the front. */
    fun add(context: Context, record: RideRecord) {
        ensureLoaded(context)
        rides = (listOf(record) + rides).sortedByDescending { it.startEpochMs }
        persist(context)
    }

    fun clear(context: Context) {
        rides = emptyList()
        runCatching { file(context).delete() }
    }

    private fun persist(context: Context) {
        runCatching { file(context).writeText(json.encodeToString(rides)) }
            .onFailure { Log.w(TAG, "persist failed: $it") }
    }

    private fun file(context: Context) = File(context.applicationContext.filesDir, FILE)
}
