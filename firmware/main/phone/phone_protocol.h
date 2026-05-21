#pragma once
#include "phone.h"
#include <stddef.h>
#include <stdint.h>

// Wire format. See phone_protocol.c for the byte layout.
//
//   header:        u8  type
//                  u16 payload_len      (little-endian)
//   NOTIF:         u32 id               (little-endian)
//                  u8  kind             (notif_kind_t)
//                  u8  sender_len
//                  ..  sender bytes     (no NUL terminator)
//                  u16 msg_len          (little-endian)
//                  ..  msg bytes        (no NUL terminator)
//   NOTIF_DISMISS: u32 id
//   MEDIA:         u8  state            (media_state_t)
//                  u8  artist_len
//                  ..  artist bytes
//                  u8  title_len
//                  ..  title bytes
//
// Strings are truncated to fit our fixed buffers (NOTIF_SENDER_MAX etc.)
// and always NUL-terminated on the way out. The wire never carries NULs.

typedef enum {
    PHONE_PARSE_OK             = 0,
    PHONE_PARSE_NEED_MORE      = 1,   // buffer cut mid-message; retry with more bytes
    PHONE_PARSE_BAD_TYPE       = 2,   // unknown message type — caller should skip
    PHONE_PARSE_BAD_FIELD      = 3,   // declared payload doesn't fit declared sub-fields
} phone_parse_result_t;

// Parse exactly one message from `buf` (`len` bytes available). On success
// fills `*out` and writes the number of bytes consumed to `*consumed`.
// On NEED_MORE, `*consumed` is 0. On BAD_TYPE / BAD_FIELD, `*consumed` is
// the size of the bad message so the caller can advance past it if they
// like (in practice the BLE write framing should make this redundant).
phone_parse_result_t phone_protocol_parse(const uint8_t   *buf,
                                          size_t           len,
                                          size_t          *consumed,
                                          phone_event_t   *out);

// Cluster → phone command stream. Same TLV framing as the RX side, just
// running the other way over the TX notify characteristic. Types are
// in a separate range so a future single-channel design could share the
// parser without ambiguity.
//
//   header:                u8  type
//                          u16 payload_len    (little-endian)
//   CMD_CALL_ACCEPT (0x10):   payload_len=0
//   CMD_CALL_REJECT (0x11):   payload_len=0
//   CMD_CALL_END    (0x12):   payload_len=0
//   CMD_MEDIA_PREV       (0x20): payload_len=0
//   CMD_MEDIA_PLAY_PAUSE (0x21): payload_len=0
//   CMD_MEDIA_NEXT       (0x22): payload_len=0
//   CMD_NOTIF_DISMISS    (0x30): payload_len=4, u32 id (matches RX NOTIF.id)
typedef enum {
    PHONE_CMD_CALL_ACCEPT     = 0x10,
    PHONE_CMD_CALL_REJECT     = 0x11,
    PHONE_CMD_CALL_END        = 0x12,
    PHONE_CMD_MEDIA_PREV      = 0x20,
    PHONE_CMD_MEDIA_PLAY_PAUSE = 0x21,
    PHONE_CMD_MEDIA_NEXT      = 0x22,
    PHONE_CMD_NOTIF_DISMISS   = 0x30,
} phone_cmd_t;

// Encode a no-payload command into out[]. Returns the number of bytes
// written (always 3) or 0 if out_sz < 3.
size_t phone_protocol_encode_cmd(phone_cmd_t cmd, uint8_t *out, size_t out_sz);

// Encode a NOTIF_DISMISS command carrying an id. Returns the number of
// bytes written (always 7) or 0 if out_sz < 7.
size_t phone_protocol_encode_dismiss(uint32_t id, uint8_t *out, size_t out_sz);
