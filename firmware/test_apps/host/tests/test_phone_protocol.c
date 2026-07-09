#include "unity.h"
#include "phone_protocol.h"
#include <string.h>

// --- helpers ----------------------------------------------------------------

// Write a little-endian uint16 into buf, return bytes written.
static size_t put_u16(uint8_t *buf, uint16_t v)
{
    buf[0] = v & 0xFF;
    buf[1] = (v >> 8) & 0xFF;
    return 2;
}

static size_t put_u32(uint8_t *buf, uint32_t v)
{
    buf[0] = v & 0xFF;
    buf[1] = (v >> 8) & 0xFF;
    buf[2] = (v >> 16) & 0xFF;
    buf[3] = (v >> 24) & 0xFF;
    return 4;
}

// Build a wire-format NOTIF message into buf. Returns total bytes.
static size_t build_notif(uint8_t *buf, uint32_t id, notif_kind_t kind,
                          const char *sender, const char *message)
{
    uint8_t  slen = (uint8_t)strlen(sender);
    uint16_t mlen = (uint16_t)strlen(message);
    uint16_t payload = 4 + 1 + 1 + slen + 2 + mlen;

    size_t i = 0;
    buf[i++] = PHONE_EVT_NOTIF;
    i += put_u16(buf + i, payload);
    i += put_u32(buf + i, id);
    buf[i++] = (uint8_t)kind;
    buf[i++] = slen;
    memcpy(buf + i, sender, slen); i += slen;
    i += put_u16(buf + i, mlen);
    memcpy(buf + i, message, mlen); i += mlen;
    return i;
}

static size_t build_dismiss(uint8_t *buf, uint32_t id)
{
    size_t i = 0;
    buf[i++] = PHONE_EVT_NOTIF_DISMISS;
    i += put_u16(buf + i, 4);   // payload = u32 id
    i += put_u32(buf + i, id);
    return i;
}

static size_t build_media(uint8_t *buf, media_state_t state,
                          const char *artist, const char *title)
{
    uint8_t alen = (uint8_t)strlen(artist);
    uint8_t tlen = (uint8_t)strlen(title);
    uint16_t payload = 1 + 1 + alen + 1 + tlen;

    size_t i = 0;
    buf[i++] = PHONE_EVT_MEDIA;
    i += put_u16(buf + i, payload);
    buf[i++] = (uint8_t)state;
    buf[i++] = alen;
    memcpy(buf + i, artist, alen); i += alen;
    buf[i++] = tlen;
    memcpy(buf + i, title, tlen); i += tlen;
    return i;
}

// --- happy-path parses ------------------------------------------------------

static void test_parse_notif(void)
{
    uint8_t       buf[256];
    size_t        n = build_notif(buf, 0xABCD1234, NOTIF_KIND_CALL, "John", "ringing");
    size_t        consumed = 0;
    phone_event_t evt;

    TEST_ASSERT_EQUAL_INT(PHONE_PARSE_OK,
                          phone_protocol_parse(buf, n, &consumed, &evt));
    TEST_ASSERT_EQUAL_size_t(n, consumed);
    TEST_ASSERT_EQUAL_INT(PHONE_EVT_NOTIF, evt.type);
    TEST_ASSERT_TRUE(evt.notif.active);
    TEST_ASSERT_EQUAL_UINT32(0xABCD1234u, evt.notif.id);
    TEST_ASSERT_EQUAL_INT(NOTIF_KIND_CALL, evt.notif.kind);
    TEST_ASSERT_EQUAL_STRING("John",    evt.notif.sender);
    TEST_ASSERT_EQUAL_STRING("ringing", evt.notif.message);
    TEST_ASSERT_EQUAL_UINT32(0u, evt.notif.icon_id);  // absent -> 0 (backward compat)
}

static void test_parse_notif_with_icon_id(void)
{
    // NOTIF payload with a trailing icon_id: id, kind, slen "Al", mlen "hi", icon.
    uint8_t p[] = {
        0x11,
        0x22,
        0x33,
        0x44,            // id
        NOTIF_KIND_APP,  // kind
        2,
        'A',
        'l',  // sender len + bytes
        2,
        0,
        'h',
        'i',  // msg len (LE) + bytes
        0xEF,
        0xBE,
        0xAD,
        0xDE,  // icon_id 0xDEADBEEF (LE)
    };
    uint8_t buf[64];
    buf[0] = PHONE_EVT_NOTIF;
    buf[1] = (uint8_t)sizeof(p);
    buf[2] = 0;
    memcpy(buf + 3, p, sizeof(p));
    size_t        consumed = 0;
    phone_event_t evt;
    TEST_ASSERT_EQUAL_INT(PHONE_PARSE_OK,
                          phone_protocol_parse(buf, 3 + sizeof(p), &consumed, &evt));
    TEST_ASSERT_EQUAL_INT(PHONE_EVT_NOTIF, evt.type);
    TEST_ASSERT_EQUAL_STRING("Al", evt.notif.sender);
    TEST_ASSERT_EQUAL_STRING("hi", evt.notif.message);
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, evt.notif.icon_id);
}

static void test_parse_icon_chunk(void)
{
    uint8_t p[] = {
        0x21, 0x43, 0x65, 0x87,  // icon_id 0x87654321 (LE)
        0x00, 0x12,              // total_len 4608 (LE)
        0x40, 0x00,              // offset 64 (LE)
        0xAA, 0xBB, 0xCC,        // 3 chunk bytes
    };
    uint8_t buf[64];
    buf[0] = PHONE_EVT_ICON;
    buf[1] = (uint8_t)sizeof(p);
    buf[2] = 0;
    memcpy(buf + 3, p, sizeof(p));
    size_t        consumed = 0;
    phone_event_t evt;
    TEST_ASSERT_EQUAL_INT(PHONE_PARSE_OK,
                          phone_protocol_parse(buf, 3 + sizeof(p), &consumed, &evt));
    TEST_ASSERT_EQUAL_INT(PHONE_EVT_ICON, evt.type);
    TEST_ASSERT_EQUAL_UINT32(0x87654321u, evt.icon.icon_id);
    TEST_ASSERT_EQUAL_UINT16(4608, evt.icon.total_len);
    TEST_ASSERT_EQUAL_UINT16(64, evt.icon.offset);
    TEST_ASSERT_EQUAL_UINT16(3, evt.icon.len);
    TEST_ASSERT_EQUAL_UINT8(0xAA, evt.icon.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0xCC, evt.icon.data[2]);
}

static void test_parse_icon_too_short_rejected(void)
{
    uint8_t buf[16];
    buf[0] = PHONE_EVT_ICON;
    buf[1] = 7;
    buf[2] = 0;  // payload 7 < 8
    memset(buf + 3, 0, 7);
    size_t        consumed = 0;
    phone_event_t evt;
    TEST_ASSERT_EQUAL_INT(PHONE_PARSE_BAD_FIELD, phone_protocol_parse(buf, 10, &consumed, &evt));
}

static void test_parse_dismiss(void)
{
    uint8_t       buf[16];
    size_t        n = build_dismiss(buf, 0x42);
    size_t        consumed = 0;
    phone_event_t evt;

    TEST_ASSERT_EQUAL_INT(PHONE_PARSE_OK,
                          phone_protocol_parse(buf, n, &consumed, &evt));
    TEST_ASSERT_EQUAL_size_t(n, consumed);
    TEST_ASSERT_EQUAL_INT(PHONE_EVT_NOTIF_DISMISS, evt.type);
    TEST_ASSERT_EQUAL_UINT32(0x42u, evt.dismiss_id);
}

static void test_parse_media(void)
{
    uint8_t       buf[128];
    size_t        n = build_media(buf, MEDIA_STATE_PLAYING, "Ramones", "Blitzkrieg Bop");
    size_t        consumed = 0;
    phone_event_t evt;

    TEST_ASSERT_EQUAL_INT(PHONE_PARSE_OK,
                          phone_protocol_parse(buf, n, &consumed, &evt));
    TEST_ASSERT_EQUAL_size_t(n, consumed);
    TEST_ASSERT_EQUAL_INT(PHONE_EVT_MEDIA, evt.type);
    TEST_ASSERT_EQUAL_INT(MEDIA_STATE_PLAYING, evt.media.state);
    TEST_ASSERT_EQUAL_STRING("Ramones",        evt.media.artist);
    TEST_ASSERT_EQUAL_STRING("Blitzkrieg Bop", evt.media.title);
}

// --- truncation / framing ---------------------------------------------------

static void test_truncated_header_returns_need_more(void)
{
    uint8_t       buf[2] = { PHONE_EVT_NOTIF_DISMISS, 0x04 };  // header is 3 bytes
    size_t        consumed = 99;
    phone_event_t evt;
    TEST_ASSERT_EQUAL_INT(PHONE_PARSE_NEED_MORE,
                          phone_protocol_parse(buf, 2, &consumed, &evt));
    TEST_ASSERT_EQUAL_size_t(0, consumed);
}

static void test_truncated_payload_returns_need_more(void)
{
    // Build a valid notif but pass only the first 8 bytes (less than full).
    uint8_t       buf[256];
    size_t        n = build_notif(buf, 1, NOTIF_KIND_SMS, "Alice", "hey");
    size_t        consumed = 0;
    phone_event_t evt;
    TEST_ASSERT_TRUE(n > 8);
    TEST_ASSERT_EQUAL_INT(PHONE_PARSE_NEED_MORE,
                          phone_protocol_parse(buf, 8, &consumed, &evt));
    TEST_ASSERT_EQUAL_size_t(0, consumed);
}

// --- error cases ------------------------------------------------------------

static void test_unknown_type_returns_bad_type(void)
{
    uint8_t       buf[8]    = { 0xFE, 0x02, 0x00, 0xAA, 0xBB };  // type 0xFE, 2-byte payload
    size_t        consumed  = 0;
    phone_event_t evt;
    TEST_ASSERT_EQUAL_INT(PHONE_PARSE_BAD_TYPE,
                          phone_protocol_parse(buf, sizeof(buf), &consumed, &evt));
    TEST_ASSERT_EQUAL_size_t(5, consumed);   // 3 header + 2 payload — let caller skip past
}

static void test_notif_with_undersized_payload_rejected(void)
{
    // Claims payload_len = 3 (less than minimum 6 for NOTIF).
    uint8_t       buf[16] = { PHONE_EVT_NOTIF, 0x03, 0x00, 0x00, 0x00, 0x00 };
    size_t        consumed = 0;
    phone_event_t evt;
    TEST_ASSERT_EQUAL_INT(PHONE_PARSE_BAD_FIELD,
                          phone_protocol_parse(buf, sizeof(buf), &consumed, &evt));
}

static void test_dismiss_with_wrong_payload_len_rejected(void)
{
    // Dismiss declares 8-byte payload (should be 4).
    uint8_t buf[16] = {
        PHONE_EVT_NOTIF_DISMISS, 0x08, 0x00,
        1, 0, 0, 0,  0, 0, 0, 0,
    };
    size_t        consumed = 0;
    phone_event_t evt;
    TEST_ASSERT_EQUAL_INT(PHONE_PARSE_BAD_FIELD,
                          phone_protocol_parse(buf, sizeof(buf), &consumed, &evt));
}

// NOTIF where the declared payload_len passes the >=6 minimum but the
// inner sender_len + msg_len header don't fit. Triggers the second
// guard inside the NOTIF case.
static void test_notif_inner_sender_len_overflow_rejected(void)
{
    // payload_len = 8, slen claims 10 bytes (impossible in 8-byte payload).
    uint8_t buf[16] = {
        PHONE_EVT_NOTIF,  0x08, 0x00,
        0x01, 0, 0, 0,    // id = 1
        NOTIF_KIND_CALL,
        10,               // slen — too big
        0, 0,             // msg_len placeholder
    };
    size_t        consumed = 0;
    phone_event_t evt;
    TEST_ASSERT_EQUAL_INT(PHONE_PARSE_BAD_FIELD,
                          phone_protocol_parse(buf, sizeof(buf), &consumed, &evt));
}

// NOTIF where sender fits but the declared msg_len overruns the payload.
static void test_notif_inner_msg_len_overflow_rejected(void)
{
    // payload_len = 8; slen = 0; msg_len = 10 — only 0 bytes left after header.
    uint8_t buf[16] = {
        PHONE_EVT_NOTIF,  0x08, 0x00,
        0x01, 0, 0, 0,    // id
        NOTIF_KIND_SMS,
        0,                // slen = 0
        10, 0,            // msg_len = 10, doesn't fit
    };
    size_t        consumed = 0;
    phone_event_t evt;
    TEST_ASSERT_EQUAL_INT(PHONE_PARSE_BAD_FIELD,
                          phone_protocol_parse(buf, sizeof(buf), &consumed, &evt));
}

// MEDIA payload too short for state + artist_len + title_len header.
static void test_media_with_undersized_payload_rejected(void)
{
    uint8_t buf[8] = { PHONE_EVT_MEDIA, 0x02, 0x00,  0, 0 };  // payload_len = 2
    size_t        consumed = 0;
    phone_event_t evt;
    TEST_ASSERT_EQUAL_INT(PHONE_PARSE_BAD_FIELD,
                          phone_protocol_parse(buf, sizeof(buf), &consumed, &evt));
}

// MEDIA inner artist_len overruns the payload.
static void test_media_inner_artist_len_overflow_rejected(void)
{
    uint8_t buf[16] = {
        PHONE_EVT_MEDIA, 0x03, 0x00,
        MEDIA_STATE_PLAYING,
        10,           // artist_len — impossible in a 3-byte payload
        0,            // title_len placeholder
    };
    size_t        consumed = 0;
    phone_event_t evt;
    TEST_ASSERT_EQUAL_INT(PHONE_PARSE_BAD_FIELD,
                          phone_protocol_parse(buf, sizeof(buf), &consumed, &evt));
}

// MEDIA inner title_len overruns the payload.
static void test_media_inner_title_len_overflow_rejected(void)
{
    uint8_t buf[16] = {
        PHONE_EVT_MEDIA, 0x03, 0x00,
        MEDIA_STATE_PLAYING,
        0,            // artist_len = 0
        10,           // title_len = 10, doesn't fit
    };
    size_t        consumed = 0;
    phone_event_t evt;
    TEST_ASSERT_EQUAL_INT(PHONE_PARSE_BAD_FIELD,
                          phone_protocol_parse(buf, sizeof(buf), &consumed, &evt));
}

// MEDIA with an out-of-range state value (e.g. companion-app version
// mismatch) is clamped to STOPPED rather than written through.
static void test_media_unknown_state_clamps_to_stopped(void)
{
    uint8_t buf[16] = {
        PHONE_EVT_MEDIA, 0x03, 0x00,
        99,           // state — beyond MEDIA_STATE_LAST
        0,            // artist_len
        0,            // title_len
    };
    size_t        consumed = 0;
    phone_event_t evt;
    TEST_ASSERT_EQUAL_INT(PHONE_PARSE_OK,
                          phone_protocol_parse(buf, sizeof(buf), &consumed, &evt));
    TEST_ASSERT_EQUAL_INT(MEDIA_STATE_STOPPED, evt.media.state);
}

// --- truncation of oversized string fields ----------------------------------

static void test_long_sender_is_truncated_to_buffer(void)
{
    // Sender exactly NOTIF_SENDER_MAX bytes — must be truncated to MAX-1 + NUL.
    char sender[NOTIF_SENDER_MAX + 1];
    memset(sender, 'A', NOTIF_SENDER_MAX);
    sender[NOTIF_SENDER_MAX] = '\0';

    uint8_t buf[256];
    size_t  n = build_notif(buf, 1, NOTIF_KIND_APP, sender, "x");
    size_t  consumed = 0;
    phone_event_t evt;

    TEST_ASSERT_EQUAL_INT(PHONE_PARSE_OK,
                          phone_protocol_parse(buf, n, &consumed, &evt));
    TEST_ASSERT_EQUAL_size_t(NOTIF_SENDER_MAX - 1, strlen(evt.notif.sender));
    TEST_ASSERT_EQUAL_CHAR('\0', evt.notif.sender[NOTIF_SENDER_MAX - 1]);
}

static void test_unknown_notif_kind_falls_back_to_app(void)
{
    // Encode kind = 99 (>= NOTIF_KIND_LAST). Parser should clamp to APP
    // rather than write garbage into the kind field.
    uint8_t buf[64];
    size_t  n = build_notif(buf, 1, (notif_kind_t)99, "Bob", "hi");
    size_t  consumed = 0;
    phone_event_t evt;
    TEST_ASSERT_EQUAL_INT(PHONE_PARSE_OK,
                          phone_protocol_parse(buf, n, &consumed, &evt));
    TEST_ASSERT_EQUAL_INT(NOTIF_KIND_APP, evt.notif.kind);
}

// --- empty / zero-length fields ---------------------------------------------

static void test_empty_message_is_valid(void)
{
    uint8_t buf[64];
    size_t  n = build_notif(buf, 1, NOTIF_KIND_CALL, "John", "");
    size_t  consumed = 0;
    phone_event_t evt;
    TEST_ASSERT_EQUAL_INT(PHONE_PARSE_OK,
                          phone_protocol_parse(buf, n, &consumed, &evt));
    TEST_ASSERT_EQUAL_STRING("", evt.notif.message);
}

static void test_media_stopped_clears_track(void)
{
    uint8_t buf[64];
    size_t  n = build_media(buf, MEDIA_STATE_STOPPED, "", "");
    size_t  consumed = 0;
    phone_event_t evt;
    TEST_ASSERT_EQUAL_INT(PHONE_PARSE_OK,
                          phone_protocol_parse(buf, n, &consumed, &evt));
    TEST_ASSERT_EQUAL_INT(MEDIA_STATE_STOPPED, evt.media.state);
    TEST_ASSERT_EQUAL_STRING("", evt.media.artist);
    TEST_ASSERT_EQUAL_STRING("", evt.media.title);
}

// --- TX encoders (cluster → phone) ---------------------------------------

static void test_encode_cmd_no_payload(void)
{
    // CMD_MEDIA_NEXT (0x22) — type + u16 0 = three bytes total.
    uint8_t buf[8] = {0};
    size_t  n = phone_protocol_encode_cmd(PHONE_CMD_MEDIA_NEXT, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_UINT8(0x22, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[2]);
}

static void test_encode_cmd_undersized_buffer(void)
{
    // Encoder must refuse to write past the caller's buffer end.
    uint8_t buf[2] = {0xAA, 0xBB};
    size_t  n = phone_protocol_encode_cmd(PHONE_CMD_CALL_ACCEPT, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(0, n);
    TEST_ASSERT_EQUAL_UINT8(0xAA, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0xBB, buf[1]);
}

static void test_encode_dismiss_undersized_buffer(void)
{
    // Dismiss needs 7 bytes (cmd + u16 len + u32 id); 6 must fail closed.
    uint8_t buf[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    size_t  n      = phone_protocol_encode_dismiss(0xDEADBEEFu, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(0, n);
    TEST_ASSERT_EQUAL_UINT8(0xAA, buf[0]);
}

static void test_encode_dismiss_little_endian_id(void)
{
    // payload = u32 id, LE.
    uint8_t buf[8] = {0};
    size_t  n = phone_protocol_encode_dismiss(0xDEADBEEFu, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(7, n);
    TEST_ASSERT_EQUAL_UINT8(PHONE_CMD_NOTIF_DISMISS, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x04, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0xEF, buf[3]);
    TEST_ASSERT_EQUAL_UINT8(0xBE, buf[4]);
    TEST_ASSERT_EQUAL_UINT8(0xAD, buf[5]);
    TEST_ASSERT_EQUAL_UINT8(0xDE, buf[6]);
}

static void test_parse_config_speed_divisor(void)
{
    uint8_t buf[5];
    buf[0] = PHONE_EVT_CONFIG;
    put_u16(buf + 1, 2);    // payload_len
    put_u16(buf + 3, 188);  // speed_divisor

    size_t        consumed = 0;
    phone_event_t evt;
    TEST_ASSERT_EQUAL_INT(PHONE_PARSE_OK, phone_protocol_parse(buf, sizeof(buf), &consumed, &evt));
    TEST_ASSERT_EQUAL_INT(PHONE_EVT_CONFIG, evt.type);
    TEST_ASSERT_EQUAL_UINT16(188, evt.config.speed_divisor);
    TEST_ASSERT_EQUAL_size_t(5, consumed);
}

static void test_parse_config_too_short_rejected(void)
{
    uint8_t buf[4];
    buf[0] = PHONE_EVT_CONFIG;
    put_u16(buf + 1, 1);  // payload_len 1 (< 2)
    buf[3]                 = 0xBC;
    size_t        consumed = 0;
    phone_event_t evt;
    TEST_ASSERT_EQUAL_INT(PHONE_PARSE_BAD_FIELD,
                          phone_protocol_parse(buf, sizeof(buf), &consumed, &evt));
}

void RunTests(void)
{
    RUN_TEST(test_parse_notif);
    RUN_TEST(test_parse_dismiss);
    RUN_TEST(test_parse_media);
    RUN_TEST(test_truncated_header_returns_need_more);
    RUN_TEST(test_truncated_payload_returns_need_more);
    RUN_TEST(test_unknown_type_returns_bad_type);
    RUN_TEST(test_notif_with_undersized_payload_rejected);
    RUN_TEST(test_dismiss_with_wrong_payload_len_rejected);
    RUN_TEST(test_notif_inner_sender_len_overflow_rejected);
    RUN_TEST(test_notif_inner_msg_len_overflow_rejected);
    RUN_TEST(test_media_with_undersized_payload_rejected);
    RUN_TEST(test_media_inner_artist_len_overflow_rejected);
    RUN_TEST(test_media_inner_title_len_overflow_rejected);
    RUN_TEST(test_media_unknown_state_clamps_to_stopped);
    RUN_TEST(test_long_sender_is_truncated_to_buffer);
    RUN_TEST(test_unknown_notif_kind_falls_back_to_app);
    RUN_TEST(test_empty_message_is_valid);
    RUN_TEST(test_media_stopped_clears_track);
    RUN_TEST(test_encode_cmd_no_payload);
    RUN_TEST(test_encode_cmd_undersized_buffer);
    RUN_TEST(test_encode_dismiss_undersized_buffer);
    RUN_TEST(test_encode_dismiss_little_endian_id);
    RUN_TEST(test_parse_config_speed_divisor);
    RUN_TEST(test_parse_config_too_short_rejected);
    RUN_TEST(test_parse_notif_with_icon_id);
    RUN_TEST(test_parse_icon_chunk);
    RUN_TEST(test_parse_icon_too_short_rejected);
}
