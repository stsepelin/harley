package ee.zeppl.companion.notif

import ee.zeppl.companion.ble.Protocol

/**
 * Pure mapping logic between an Android [android.service.notification.StatusBarNotification]
 * and a cluster wire-format payload.
 *
 * Lives separately from [ZepplNotifListener] so we can unit-test the
 * classification rules without the Android framework. The constants
 * below mirror values on [android.app.Notification] and must stay in
 * lock-step with the platform.
 */
internal object NotifMapper {

    // Mirrors Notification.FLAG_FOREGROUND_SERVICE. Long-running app
    // state ("Spotify is playing", "Maps is navigating") arrives as a
    // notification on the system bar but isn't user-actionable on the
    // bike — drop these.
    private const val FLAG_FOREGROUND_SERVICE = 0x40

    // Mirrors Notification.FLAG_GROUP_SUMMARY. Messaging apps (Telegram,
    // Gmail, etc.) post a summary entry plus a child per message — both
    // arrive at the listener with identical title/text fields. Dropping
    // the summary keeps the cluster from showing the same message twice
    // back-to-back in its queue.
    private const val FLAG_GROUP_SUMMARY = 0x200

    // Mirrors Notification.CATEGORY_* string constants. The platform
    // never localises these so a literal match is correct.
    private const val CAT_CALL        = "call"
    private const val CAT_MISSED_CALL = "missed_call"
    private const val CAT_MESSAGE     = "msg"
    private const val CAT_TRANSPORT   = "transport"

    // Notification.EXTRA_TEMPLATE value for the MediaStyle (now-playing)
    // notification. Spotify / YouTube Music / etc. post these alongside
    // their actual MediaSession; we already push that channel via
    // MediaWatcher, so the notification copy is redundant and the
    // sender/text fields are awkwardly inverted (title goes in
    // EXTRA_TITLE, artist in EXTRA_TEXT). Drop on sight.
    //
    // Note the literal `$`: the Java class name embeds it, so when this
    // string round-trips through the platform we have to match it
    // verbatim — escape it for the Kotlin compiler only.
    private const val TEMPLATE_MEDIA_STYLE = "android.app.Notification\$MediaStyle"

    // Notification.CallStyle (API 31+) is what modern dialers use for
    // incoming and ongoing calls — Samsung Phone, Google Phone, most
    // SIP/VoIP apps. They don't always set category="call" alongside it,
    // so a category-only classifier silently drops the most important
    // notification we surface on the bike. Match on the template too.
    private const val TEMPLATE_CALL_STYLE = "android.app.Notification\$CallStyle"

    /**
     * Stable 32-bit id for a notification. The cluster uses ids to scope
     * DISMISS events to a specific notif; we need the post→cancel pair
     * to produce the same id, even across process restarts. Android's
     * own `key` (package + tag + id) is the canonical identity, so its
     * hash is the natural source.
     */
    fun stableId(key: String): UInt = key.hashCode().toUInt()

    /**
     * Map a (category, template) pair to the cluster's enum. Template wins
     * over category — CallStyle dialers post notifications without setting
     * category, so a template match has to take precedence so the call
     * doesn't get classified as a generic APP.
     */
    fun kindFor(category: String?, template: String? = null): Protocol.NotifKind = when {
        template == TEMPLATE_CALL_STYLE                  -> Protocol.NotifKind.CALL
        category == CAT_CALL || category == CAT_MISSED_CALL -> Protocol.NotifKind.CALL
        category == CAT_MESSAGE                          -> Protocol.NotifKind.SMS
        else                                             -> Protocol.NotifKind.APP
    }

    /**
     * Build the wire-format payload for a posted notification, or null
     * if the platform-side rules say to drop it. Drop rules, in order:
     * own package, ongoing (long-running app state), foreground service
     * notification, group summary (messaging apps post these alongside
     * per-message children). The user's per-app mute is checked by the
     * caller before this — [AllowList] needs a [android.content.Context]
     * and we want this function to stay pure.
     */
    fun encodePost(
        ownPackage:  String,
        packageName: String,
        isOngoing:   Boolean,
        flags:       Int,
        key:         String,
        category:    String?,
        template:    String?,
        title:       String,
        text:        String,
    ): ByteArray? {
        if (packageName == ownPackage) return null
        // Calls are *always* ongoing (the notification persists for the
        // duration of the call), and modern dialers also tag their
        // notification as a foreground service. The "drop long-running
        // state" filters below would silently swallow the incoming call,
        // which is the one notification we most want to surface — let
        // calls through both gates. Recognise both the legacy category
        // marker and the CallStyle template (API 31+ dialers).
        val isCall = category == CAT_CALL ||
                     category == CAT_MISSED_CALL ||
                     template == TEMPLATE_CALL_STYLE
        if (!isCall && isOngoing) return null
        if (!isCall && (flags and FLAG_FOREGROUND_SERVICE) != 0) return null
        if (flags and FLAG_GROUP_SUMMARY      != 0) return null
        if (category == CAT_TRANSPORT)          return null
        if (template == TEMPLATE_MEDIA_STYLE)   return null
        return Protocol.encodeNotif(stableId(key), kindFor(category, template), title, text)
    }
}
