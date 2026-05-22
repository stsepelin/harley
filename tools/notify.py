#!/usr/bin/env python3
"""
notify.py — push fake phone payloads into a running SDL2 cluster sim.

Connects to localhost:7700, where firmware/simulator/test_bridge.c
listens, and writes TLV-framed bytes that match what the companion
Android app sends to the cluster's BLE RX characteristic. The bridge
runs them through the same phone_protocol_parse + phone_data_apply
path the real cluster uses, so the same banner / media widgets render
exactly as they would on the bike.

Mirrors companion/.../Protocol.kt and firmware/main/phone/phone_protocol.c
byte-for-byte. If you touch either of those wire formats, touch this
file too — there is no version negotiation.

Usage:
    # start the sim in one terminal:
    cd firmware/simulator && cmake --build build && ./build/vrod_sim

    # drive it from another:
    tools/notify.py call "Alice"
    tools/notify.py sms "Mom" "running late"
    tools/notify.py sms "Mom" "running late \U0001F697"   # emoji works
    tools/notify.py app "Slack" "PR ready for review"
    tools/notify.py media playing "Foo Fighters" "Everlong"
    tools/notify.py media stopped
    tools/notify.py dismiss 0xABCD1234
"""
from __future__ import annotations

import argparse
import socket
import struct
import sys

HOST = "127.0.0.1"
PORT = 7700

# Mirrors phone_event_type_t in firmware/main/phone/phone.h
TYPE_NOTIF         = 0x01
TYPE_NOTIF_DISMISS = 0x02
TYPE_MEDIA         = 0x03

# Mirrors notif_kind_t
KIND_CALL = 0
KIND_SMS  = 1
KIND_APP  = 2

# Mirrors media_state_t
MEDIA_STOPPED = 0
MEDIA_PAUSED  = 1
MEDIA_PLAYING = 2

# Cluster buffer caps (phone.h). The bridge will silently truncate
# longer strings on the parse side, but we mirror here so the client
# can predict what the rider sees.
NOTIF_SENDER_MAX = 48
NOTIF_MSG_MAX    = 128
MEDIA_FIELD_MAX  = 48


def truncate(s: str, cap: int) -> bytes:
    """Truncate s to fit in cap - 1 bytes, snapping to a UTF-8 codepoint
    boundary so we never split a multi-byte sequence in half. Matches
    Protocol.kt's truncatedUtf8."""
    raw = s.encode("utf-8")
    if len(raw) <= cap - 1:
        return raw
    cut = cap - 1
    while cut > 0 and (raw[cut] & 0xC0) == 0x80:
        cut -= 1
    return raw[:cut]


def _frame(type_byte: int, payload: bytes) -> bytes:
    if len(payload) > 0xFFFF:
        raise ValueError(f"payload {len(payload)} bytes exceeds u16 length field")
    return bytes([type_byte]) + struct.pack("<H", len(payload)) + payload


def encode_notif(notif_id: int, kind: int, sender: str, message: str) -> bytes:
    s = truncate(sender, NOTIF_SENDER_MAX)
    m = truncate(message, NOTIF_MSG_MAX)
    payload = (
        struct.pack("<IBB", notif_id & 0xFFFFFFFF, kind, len(s))
        + s
        + struct.pack("<H", len(m))
        + m
    )
    return _frame(TYPE_NOTIF, payload)


def encode_dismiss(notif_id: int) -> bytes:
    return _frame(TYPE_NOTIF_DISMISS, struct.pack("<I", notif_id & 0xFFFFFFFF))


def encode_media(state: int, artist: str, title: str) -> bytes:
    a = truncate(artist, MEDIA_FIELD_MAX)
    t = truncate(title, MEDIA_FIELD_MAX)
    payload = struct.pack("<BB", state, len(a)) + a + bytes([len(t)]) + t
    return _frame(TYPE_MEDIA, payload)


def send(packet: bytes) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((HOST, PORT))
        s.sendall(packet)


def _int(x: str) -> int:
    return int(x, 0)   # base=0 → accepts 0xABCD / 0o755 / 42


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        description="Send fake BLE payloads to the SDL2 cluster simulator.",
    )
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("call", help="incoming call notification")
    p.add_argument("sender")
    p.add_argument("--id", type=_int, default=1)

    p = sub.add_parser("sms", help="SMS notification")
    p.add_argument("sender")
    p.add_argument("message")
    p.add_argument("--id", type=_int, default=2)

    p = sub.add_parser("app", help="generic app notification")
    p.add_argument("sender")
    p.add_argument("message")
    p.add_argument("--id", type=_int, default=3)

    p = sub.add_parser("media", help="now-playing snapshot")
    p.add_argument("state", choices=["stopped", "paused", "playing"])
    p.add_argument("artist", nargs="?", default="")
    p.add_argument("title",  nargs="?", default="")

    p = sub.add_parser("dismiss", help="dismiss a notification by id")
    p.add_argument("id", type=_int)

    return ap


def main(argv: list[str]) -> int:
    args = build_parser().parse_args(argv)

    if args.cmd == "call":
        pkt = encode_notif(args.id, KIND_CALL, args.sender, "")
    elif args.cmd == "sms":
        pkt = encode_notif(args.id, KIND_SMS,  args.sender, args.message)
    elif args.cmd == "app":
        pkt = encode_notif(args.id, KIND_APP,  args.sender, args.message)
    elif args.cmd == "media":
        state = {"stopped": MEDIA_STOPPED, "paused": MEDIA_PAUSED,
                 "playing": MEDIA_PLAYING}[args.state]
        pkt = encode_media(state, args.artist, args.title)
    elif args.cmd == "dismiss":
        pkt = encode_dismiss(args.id)
    else:
        return 1

    try:
        send(pkt)
    except ConnectionRefusedError:
        print(
            f"error: nothing listening on {HOST}:{PORT}. "
            "Start the sim first: cd firmware/simulator && ./build/vrod_sim",
            file=sys.stderr,
        )
        return 2
    print(f"sent {len(pkt)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
