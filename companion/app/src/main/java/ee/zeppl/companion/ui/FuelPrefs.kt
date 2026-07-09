package ee.zeppl.companion.ui

import android.content.Context

/**
 * Persists the fuel-economy calibration (mL per injector tick), solved from a
 * fill-up in [FuelEconomyCard]. Durable across restarts, unlike the in-memory
 * [AppPrefs] - a calibration is expensive to redo, so it has to survive.
 */
object FuelPrefs {

    private const val PREFS = "zeppl.fuel"
    private const val KEY_ML_PER_TICK = "ml_per_tick"

    /** mL per tick, or 0.0 if never calibrated. */
    fun mlPerTick(context: Context): Double =
        prefs(context).getFloat(KEY_ML_PER_TICK, 0f).toDouble()

    fun setMlPerTick(context: Context, value: Double) {
        prefs(context).edit().putFloat(KEY_ML_PER_TICK, value.toFloat()).apply()
    }

    fun isCalibrated(context: Context): Boolean = mlPerTick(context) > 0.0

    private fun prefs(context: Context) =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
}
