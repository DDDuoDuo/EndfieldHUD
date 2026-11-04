#!/usr/bin/env python3
"""Draw the open and closed grab-hand cursors of the Windows HUD (no inputs, deterministic).

The redesigned Settings > Modules grip and the Map ask for NSCursor.openHand / closedHand on the Mac
(SystemHUDView.designCursor). Windows has no system equivalent, and the Apple artwork is not used: these are
original drawings in the same spirit (a small white hand with a black outline, palm towards the viewer, fingers
apart or curled into a fist), written as 32-bit alpha .cur files at the standard cursor sizes 32, 48 and 64 px
(100 / 150 / 200 % display scale; LoadImage(LR_DEFAULTSIZE) takes the system cursor size). The hot spot is the
middle of the palm, like the Mac's. EndfieldHUD.exe embeds them (app/EndfieldHUD.rc.in, ids in
app/overlay_host.hpp).

  hand_cursors.py            write windows/resources/app/cursors/{open-hand,closed-hand}.cur
  hand_cursors.py --check    verify the committed files equal a fresh drawing
"""
import argparse
import math
import pathlib
import struct
import sys

SIZES = (32, 48, 64)
SUPERSAMPLE = 8
OUTLINE = 1.15  # outline width in 32-px design units
HOTSPOT = (16.0, 16.5)


def capsule(ax, ay, bx, by, r):
    def sdf(x, y):
        dx, dy = bx - ax, by - ay
        t = max(0.0, min(1.0, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy)))
        return math.hypot(x - (ax + t * dx), y - (ay + t * dy)) - r
    return sdf


def rounded_rect(x0, y0, x1, y1, r):
    cx, cy, hx, hy = (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2 - r, (y1 - y0) / 2 - r
    def sdf(x, y):
        qx, qy = abs(x - cx) - hx, abs(y - cy) - hy
        return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - r
    return sdf


# Each hand: `digits` are drawn back to front, each with its own outline, so the gaps between neighbouring
# fingers show as dark creases; `body` (palm, thumb) is drawn over their bases without an inner outline; the
# outer outline of the whole hand is drawn last.
OPEN = {
    "digits": [capsule(21.6, 16.5, 22.0, 9.4, 1.75),   # little finger
               capsule(18.3, 15.5, 18.5, 6.4, 1.9),    # ring finger
               capsule(14.9, 15.5, 14.9, 5.4, 1.9),    # middle finger
               capsule(11.5, 16.0, 11.2, 7.2, 1.9)],   # index finger
    "body": [rounded_rect(9.4, 14.0, 23.6, 26.6, 4.2),  # palm
             capsule(11.2, 21.0, 6.4, 15.6, 1.95)],     # thumb
}
CLOSED = {
    "digits": [capsule(21.4, 15.0, 21.4, 12.6, 1.85),  # curled fingers: the knuckle row over the fist
               capsule(18.2, 15.0, 18.2, 11.6, 1.9),
               capsule(14.9, 15.0, 14.9, 11.4, 1.9),
               capsule(11.6, 15.0, 11.6, 12.2, 1.9)],
    "body": [rounded_rect(9.4, 13.6, 23.6, 26.0, 4.2),
             capsule(11.4, 20.6, 8.0, 16.6, 1.95)],
}


def coverage(hand, x, y):
    """Grey level (1 white, 0 black) of one design-space sample; None outside the hand."""
    colour = None
    for sdf in hand["digits"]:
        d = sdf(x, y)
        if d <= 0:
            colour = 1.0
        elif d <= OUTLINE:
            colour = 0.0
    for sdf in hand["body"]:
        if sdf(x, y) <= 0:
            colour = 1.0
    outer = min(sdf(x, y) for sdf in hand["digits"] + hand["body"])
    if 0 < outer <= OUTLINE:
        colour = 0.0
    return colour


def draw(hand, size):
    scale = 32.0 / size
    pixels = []
    n = SUPERSAMPLE
    for py in range(size):
        row = []
        for px in range(size):
            white = alpha = 0
            for sy in range(n):
                for sx in range(n):
                    value = coverage(hand, (px + (sx + .5) / n) * scale, (py + (sy + .5) / n) * scale)
                    if value is not None:
                        alpha += 1
                        white += value
            a = alpha / (n * n)
            c = white / alpha if alpha else 0.0
            row.append((round(c * 255), round(a * 255)))
        pixels.append(row)
    return pixels


def cur(hand):
    images = []
    for size in SIZES:
        pixels = draw(hand, size)
        xor = bytearray()
        for row in reversed(pixels):  # bottom-up
            for c, a in row:
                xor += bytes((c, c, c, a))  # straight (not premultiplied) BGRA
        stride = ((size + 31) // 32) * 4
        mask = bytes(stride * size)  # all opaque in the AND mask: the alpha channel decides
        header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, len(xor) + len(mask), 0, 0, 0, 0)
        hx, hy = round(HOTSPOT[0] * size / 32), round(HOTSPOT[1] * size / 32)
        images.append((size, hx, hy, header + bytes(xor) + mask))
    out = bytearray(struct.pack("<HHH", 0, 2, len(images)))
    offset = 6 + 16 * len(images)
    for size, hx, hy, data in images:
        out += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, hx, hy, len(data), offset)
        offset += len(data)
    for *_, data in images:
        out += data
    return bytes(out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent / "resources" / "app" / "cursors"
    files = {"open-hand.cur": cur(OPEN), "closed-hand.cur": cur(CLOSED)}
    if args.check:
        stale = [name for name, data in files.items() if not (root / name).exists() or (root / name).read_bytes() != data]
        if stale:
            print("stale: " + ", ".join(stale), file=sys.stderr)
            return 1
        return 0
    root.mkdir(parents=True, exist_ok=True)
    for name, data in files.items():
        (root / name).write_bytes(data)
    return 0


if __name__ == "__main__":
    sys.exit(main())
