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

        out->type            = PHONE_EVT_NOTIF;
        out->notif.active    = true;
        out->notif.id        = id;
        out->notif.kind      = (kind < NOTIF_KIND_LAST) ? (notif_kind_t)kind : NOTIF_KIND_APP;
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

    default:
        *consumed = total_len;
        return PHONE_PARSE_BAD_TYPE;
    }
}
