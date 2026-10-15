#!/usr/bin/env python3
"""splash_bar_colours.py - the loader's progress bar in SuperTux colours (Hurrican CD32X's tool,
other colours).

    python3 tools/splash_bar_colours.py IMAGE.RAW

The bar is drawn by the boot path with palette entries 253 (empty), 254 (fill) and 255 (frame)
of IMAGE.RAW (tools/make_boot_image.py sets them to grey / red / light grey). This rewrites only
those three palette words (32X format, big-endian, after the 320 x 224 picture): a dark blue
empty bar, a light blue fill (SuperTux's ice) and a white frame. No code changes - the picture's data only."""
import struct
import sys

W, H = 320, 224


def to32x(r, g, b):
    v = (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10
    return v or 0x0400


path = sys.argv[1]
data = bytearray(open(path, "rb").read())
assert len(data) == W * H + 512, "%s: not a 320x224 IMAGE.RAW" % path
for index, rgb in ((253, (16, 24, 64)), (254, (120, 200, 248)), (255, (240, 248, 255))):
    struct.pack_into(">H", data, W * H + 2 * index, to32x(*rgb))
open(path, "wb").write(bytes(data))
print("%s: bar colours set (empty, fill, frame)" % path)
