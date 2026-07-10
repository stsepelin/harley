#include "phone_protocol.h"
#include <string.h>

// Little-endian readers. The companion app encodes LE; this matches our
// RISC-V LE target so on-device we could memcpy in many places, but
// going byte-by-byte keeps the parser portable and host-testable.
static inline uint16_t rd_u16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static inline uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

// Copy up to `cap-1` bytes from src[len] into dst and NUL-terminate.
// The wire format never carries NULs in string fields, so truncation is
// the only thing we need to handle. All call sites pass sizeof(fixed
// char buffer), which is statically >= 1, so no `cap == 0` guard.
static void copy_str(char *dst, size_t cap, const uint8_t *src, size_t len)
{
    if (len >= cap) len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

#define HEADER_BYTES   3   // u8 type + u16 payload_len

phone_parse_result_t phone_protocol_parse(const uint8_t  *buf,
                                          size_t          len,
                                          size_t         *consumed,
                                          phone_event_t  *out)
{
    *consumed = 0;
    if (len < HEADER_BYTES) return PHONE_PARSE_NEED_MORE;

    uint8_t  type        = buf[0];
    uint16_t payload_len = rd_u16(buf + 1);
    size_t   total_len   = HEADER_BYTES + payload_len;

    if (len < total_len) return PHONE_PARSE_NEED_MORE;

    const uint8_t *p   = buf + HEADER_BYTES;
    const uint8_t *end = p + payload_len;

    switch (type) {
    case PHONE_EVT_NOTIF: {
        // Need at least: u32 id + u8 kind + u8 sender_len = 6 bytes.
        if (payload_len < 6) { *consumed = total_len; return PHONE_PARSE_BAD_FIELD; }
        uint32_t id  = rd_u32(p); p += 4;
        uint8_t kind = *p++;
        uint8_t slen = *p++;
        if (p + slen + 2 > end) { *consumed = total_len; return PHONE_PARSE_BAD_FIELD; }
        const uint8_t *sender = p; p += slen;
        uint16_t mlen = rd_u16(p); p += 2;
        if (p + mlen > end) { *consumed = total_len; return PHONE_PARSE_BAD_FIELD; }
        const uint8_t *msg = p;
        p += mlen;
        // Optional trailing icon_id (u32) — length-keyed so an older phone that
        // doesn't send it still parses. memset above defaults it to 0 (no icon).
        uint32_t icon_id = (p + 4 <= end) ? rd_u32(p) : 0;

        // Zero the struct first — call_in_progress / call_start_ms aren't on
        // the wire (they're cluster-side state set by phone_data_call_accept),
        // and the caller hands us an uninitialised stack-local phone_event_t.
        // Skipping this leaks whatever garbage was on the stack into those
        // fields, which made every incoming CALL render in IN-CALL mode
        // (END CALL button) instead of the incoming REJECT/ACCEPT layout.
        memset(&out->notif, 0, sizeof(out->notif));
        out->type            = PHONE_EVT_NOTIF;
        out->notif.active    = true;
        out->notif.id        = id;
        out->notif.kind      = (kind < NOTIF_KIND_LAST) ? (notif_kind_t)kind : NOTIF_KIND_APP;
        out->notif.icon_id   = icon_id;
        copy_str(out->notif.sender,  sizeof(out->notif.sender),  sender, slen);
        copy_str(out->notif.message, sizeof(out->notif.message), msg,    mlen);
        *consumed = total_len;
        return PHONE_PARSE_OK;
    }

    case PHONE_EVT_NOTIF_DISMISS: {
        if (payload_len != 4) { *consumed = total_len; return PHONE_PARSE_BAD_FIELD; }
        out->type       = PHONE_EVT_NOTIF_DISMISS;
        out->dismiss_id = rd_u32(p);
        *consumed = total_len;
        return PHONE_PARSE_OK;
    }

    case PHONE_EVT_MEDIA: {
        // Need at least: u8 state + u8 artist_len + u8 title_len = 3 bytes.
        if (payload_len < 3) { *consumed = total_len; return PHONE_PARSE_BAD_FIELD; }
        uint8_t state = *p++;
        uint8_t alen  = *p++;
        if (p + alen + 1 > end) { *consumed = total_len; return PHONE_PARSE_BAD_FIELD; }
        const uint8_t *artist = p; p += alen;
        uint8_t tlen  = *p++;
        if (p + tlen > end) { *consumed = total_len; return PHONE_PARSE_BAD_FIELD; }
        const uint8_t *title = p;

        out->type        = PHONE_EVT_MEDIA;
        out->media.state = (state < MEDIA_STATE_LAST) ? (media_state_t)state : MEDIA_STATE_STOPPED;
        copy_str(out->media.artist, sizeof(out->media.artist), artist, alen);
        copy_str(out->media.title,  sizeof(out->media.title),  title,  tlen);
        *consumed = total_len;
        return PHONE_PARSE_OK;
    }

    case PHONE_EVT_CONFIG: {
        // Field-keyed: a run of {u8 field_id, u8 len, value[len]} sub-fields, so
        // each setting is written independently. Unknown ids are skipped by len.
        out->type                     = PHONE_EVT_CONFIG;
        out->config.has_speed_divisor = false;
        out->config.has_layout        = false;
        const uint8_t *q              = p;
        const uint8_t *pend           = p + payload_len;
        while (q + 2 <= pend) {
            uint8_t fid  = q[0];
            uint8_t flen = q[1];
            q += 2;
            if (q + flen > pend)
                break;  // truncated value: stop, keep what parsed cleanly
            if (fid == CONFIG_FIELD_SPEED_DIVISOR && flen == 2) {
                out->config.speed_divisor     = (uint16_t)(q[0] | (q[1] << 8));
                out->config.has_speed_divisor = true;
            } else if (fid == CONFIG_FIELD_LAYOUT && flen == 1) {
                out->config.layout     = q[0];
                out->config.has_layout = true;
            }
            q += flen;  // unknown ids fall through to here and are skipped
        }
        *consumed = total_len;
        return PHONE_PARSE_OK;
    }

    case PHONE_EVT_ICON: {
        // u32 icon_id + u16 total_len + u16 offset + chunk bytes. data points
        // into buf (valid only until the caller reuses it).
        if (payload_len < 8) {
            *consumed = total_len;
            return PHONE_PARSE_BAD_FIELD;
        }
        out->type         = PHONE_EVT_ICON;
        out->icon.icon_id = rd_u32(p);
        p += 4;
        out->icon.total_len = rd_u16(p);
        p += 2;
        out->icon.offset = rd_u16(p);
        p += 2;
        out->icon.data = p;
        out->icon.len  = (uint16_t)(end - p);
        *consumed      = total_len;
        return PHONE_PARSE_OK;
    }

    case PHONE_EVT_CALL_ACTIVE:
    case PHONE_EVT_CALL_END:
        // Payload-less phone-side call transitions.
        out->type = (phone_event_type_t)type;
        *consumed = total_len;
        return PHONE_PARSE_OK;

    case PHONE_EVT_LOCATION: {
        // i32 lat_e7 + i32 lon_e7 + u16 heading_cd. Length-keyed: a heading-less
        // sender (payload 8) still parses, heading defaults to unknown.
        if (payload_len < 8) {
            *consumed = total_len;
            return PHONE_PARSE_BAD_FIELD;
        }
        out->type                = PHONE_EVT_LOCATION;
        out->location.lat_e7     = (int32_t)rd_u32(p);
        out->location.lon_e7     = (int32_t)rd_u32(p + 4);
        out->location.heading_cd = (payload_len >= 10) ? rd_u16(p + 8) : 0xFFFF;
        *consumed                = total_len;
        return PHONE_PARSE_OK;
    }

    default:
        *consumed = total_len;
        return PHONE_PARSE_BAD_TYPE;
    }
}

// Little-endian writers, mirror the rd_* helpers above.
static inline void wr_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v       & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}
static inline void wr_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v        & 0xFF);
    p[1] = (uint8_t)((v >> 8)  & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

size_t phone_protocol_encode_cmd(phone_cmd_t cmd, uint8_t *out, size_t out_sz)
{
    if (out_sz < 3) return 0;
    out[0] = (uint8_t)cmd;
    wr_u16(out + 1, 0);
    return 3;
}

size_t phone_protocol_encode_dismiss(uint32_t id, uint8_t *out, size_t out_sz)
{
    if (out_sz < 7) return 0;
    out[0] = (uint8_t)PHONE_CMD_NOTIF_DISMISS;
    wr_u16(out + 1, 4);
    wr_u32(out + 3, id);
    return 7;
}
