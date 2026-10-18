#!/usr/bin/env python3
"""Writes windows/tests/fixtures/app_shortcut_packaged_logo.png.

A synthetic packaged-application logo (no installed application is read):
44 x 44 RGBA, transparent, with an opaque square of (40, 160, 90) over
[8, 36) x [8, 36) and a half-transparent (40, 160, 90, 128) one-pixel ring
around it, the way package Square44x44Logo assets carry a plated glyph with
soft edges. App shortcut icon tests and hidden coverage decode it through the
same WIC path as a real package logo (native/app_shortcut_icons.cpp).

Deterministic: stdlib zlib at level 9, no timestamps or ancillary chunks.
Usage: python3 -I windows/tools/app_shortcut_logo_fixture.py [output.png]
"""
import pathlib
import struct
import sys
import zlib

SIDE = 44
INNER = (8, 36)
COLOR = (40, 160, 90)


def pixel(x, y):
    lo, hi = INNER
    if lo <= x < hi and lo <= y < hi:
        return COLOR + (255,)
    if lo - 1 <= x < hi + 1 and lo - 1 <= y < hi + 1:
        return COLOR + (128,)
    return (0, 0, 0, 0)


def chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def png():
    rows = b"".join(b"\x00" + b"".join(bytes(pixel(x, y)) for x in range(SIDE)) for y in range(SIDE))
    header = struct.pack(">IIBBBBB", SIDE, SIDE, 8, 6, 0, 0, 0)  # 8-bit RGBA, no interlace
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b"")


def main():
    root = pathlib.Path(__file__).resolve().parents[1]
    out = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / "tests" / "fixtures" / "app_shortcut_packaged_logo.png"
    out.write_bytes(png())
    print(out)


if __name__ == "__main__":
    main()
