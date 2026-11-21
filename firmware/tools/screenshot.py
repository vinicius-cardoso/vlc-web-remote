#!/usr/bin/env python3
"""Capture the knob's screens over USB serial as PNGs (real device pixels).

Puts the firmware in demo mode (made-up status, no traffic to VLC), steps
through the screens with simulated knob/button input, saves each frame, then
returns the device to live mode.

    python firmware/tools/screenshot.py [out_dir] [--port /dev/ttyACM0] [--scale 2]

Needs pyserial (PlatformIO's own Python has it). No other dependencies.
"""
import argparse
import pathlib
import struct
import sys
import time
import zlib

import serial

# name, console commands to run before the shot
SCREENS = [
    ("home", ["demo play"]),
    ("volume-boost", ["demo play", "turn 20"]),
    ("seek", ["demo play", "click", "turn 3"]),
    ("menu", ["demo play", "hold"]),
    ("audio", ["demo play", "hold", "click"]),
    ("subtitles", ["demo play", "hold", "turn 1", "click"]),
    ("vlc-closed", ["demo novlc"]),
    ("no-server", ["demo noserver"]),
]


def command(port, line):
    port.write((line + "\n").encode())
    deadline = time.time() + 3
    while time.time() < deadline:
        reply = port.readline().decode(errors="replace").strip()
        if reply == "OK":
            return
        if reply.startswith("?"):
            sys.exit(f"device rejected command: {line}")
    sys.exit(f"no reply to: {line}")


def grab(port):
    port.write(b"shot\n")
    deadline = time.time() + 5
    while True:
        if time.time() > deadline:
            sys.exit("no FRAME header")
        line = port.readline().decode(errors="replace").strip()
        if line.startswith("FRAME "):
            w, h = map(int, line.split()[1:3])
            break
    need = w * h * 2
    data = bytearray()
    while len(data) < need:
        chunk = port.read(need - len(data))
        if not chunk:
            sys.exit(f"short frame: {len(data)} of {need} bytes")
        data += chunk
    while port.readline().decode(errors="replace").strip() != "END":
        pass
    port.readline()  # the console's OK
    return w, h, data


def to_png(path, w, h, data, scale):
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            (v,) = struct.unpack_from("<H", data, (y * w + x) * 2)
            r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
            px = bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
            row += px * scale
        line = b"\x00" + bytes(row)
        rows.extend([line] * scale)

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    ihdr = struct.pack(">IIBBBBB", w * scale, h * scale, 8, 2, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
    png += chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")
    path.write_bytes(png)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out", nargs="?", default="docs/assets/knob")
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--scale", type=int, default=2)
    args = ap.parse_args()

    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    with serial.Serial(args.port, 115200, timeout=1) as port:
        time.sleep(0.3)
        port.reset_input_buffer()
        try:
            for name, cmds in SCREENS:
                for c in cmds[:1] + ["home"] + cmds[1:]:
                    command(port, c)
                    time.sleep(0.15)  # let the UI tick consume the input
                time.sleep(0.2)
                w, h, data = grab(port)
                path = out / f"{name}.png"
                to_png(path, w, h, data, args.scale)
                print(path)
        finally:
            command(port, "live")


if __name__ == "__main__":
    main()
