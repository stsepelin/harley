package ee.zeppl.companion.ble

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.drawable.Drawable
import android.util.Log

/**
 * Streams a source app's launcher icon to the cluster as PHONE_EVT_ICON chunks
 * so the notification banner can show it. Each app's icon is sent at most once
 * per connection ([reset] on disconnect); notifications then just carry the
 * icon_id ([Protocol.encodeNotif]). The pixel math is in [IconCodec].
 */
object IconSender {

    private const val TAG = "IconSender"
    // Keep each ICON frame inside one BLE write: MTU 247 -> 244 ATT payload,
    // minus the 3-byte TLV header and 8-byte icon header. 200 leaves margin for
    // a smaller negotiated MTU.
    private const val CHUNK = 200

    private val sent = mutableSetOf<UInt>()

    /** Forget what's been sent so icons re-stream after a reconnect (the cluster
     *  may have power-cycled and lost its cache). */
    @Synchronized
    fun reset() = sent.clear()

    /**
     * Ensure the icon for [packageName] has been streamed this session, and
     * return its icon_id (0 if the icon couldn't be produced). Sends the chunks
     * through [OutboundSink] (queued by the BLE client).
     */
    @Synchronized
    fun sendIfNeeded(context: Context, packageName: String): UInt {
        val id = IconCodec.iconId(packageName)
        if (id in sent) return id

        val rgb565 = try {
            renderIcon(context, packageName)
        } catch (t: Throwable) {
            Log.w(TAG, "icon render failed for $packageName: $t")
            return 0u
        } ?: return 0u

        var offset = 0
        while (offset < rgb565.size) {
            val end = minOf(offset + CHUNK, rgb565.size)
            OutboundSink.send(Protocol.encodeIcon(id, rgb565.size, offset, rgb565.copyOfRange(offset, end)))
            offset = end
        }
        sent.add(id)
        return id
    }

    private fun renderIcon(context: Context, packageName: String): ByteArray? {
        val drawable: Drawable = context.packageManager.getApplicationIcon(packageName)
        val w = Protocol.ICON_W
        val h = Protocol.ICON_H
        val bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bmp)
        drawable.setBounds(0, 0, w, h)
        drawable.draw(canvas)
        val argb = IntArray(w * h)
        bmp.getPixels(argb, 0, w, 0, 0, w, h)
        bmp.recycle()
        return IconCodec.argbToRgb565Opaque(argb)
    }
}
