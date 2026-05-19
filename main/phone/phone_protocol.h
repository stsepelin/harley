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
