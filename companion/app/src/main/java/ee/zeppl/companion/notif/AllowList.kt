package ee.zeppl.companion.notif

import android.content.Context

/**
 * Per-package notification policy. Default-allow: every installed app
 * forwards unless the user explicitly mutes it in [ee.zeppl.companion.ui.AppListScreen].
 *
 * Stored as the *blocked* set, not the allowed set, for two reasons:
 *
 *   - It encodes the right default (empty → everything allowed), so
 *     a fresh install or a newly-installed app Just Works without an
 *     extra "auto-add on install" listener.
 *   - It scales with what the user actually cares about (a handful of
 *     muted apps) rather than what they don't (the entire app list).
 *
 * SharedPrefs key is `blocked_v2`: the v1 key stored allowed packages
 * under the opposite default, and re-interpreting it would silently
 * mute apps the user never picked. v2 just starts fresh.
 */
object AllowList {

    private const val PREFS       = "zeppl.allowlist"
    private const val KEY_BLOCKED = "blocked_v2"

    /** Packages the user has explicitly muted. */
    fun blocked(context: Context): Set<String> =
        prefs(context).getStringSet(KEY_BLOCKED, emptySet()) ?: emptySet()

    fun isAllowed(context: Context, pkg: String): Boolean =
        pkg !in blocked(context)

    /** UI semantics: `allowed == true` removes from the blocked set. */
    fun set(context: Context, pkg: String, allowed: Boolean) {
        val next = blocked(context).toMutableSet().apply {
            if (allowed) remove(pkg) else add(pkg)
        }
        prefs(context).edit().putStringSet(KEY_BLOCKED, next).apply()
    }

    private fun prefs(context: Context) =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
}
