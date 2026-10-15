#!/usr/bin/env python3
"""make_stx_splash.py - the boot / loading picture for the SuperTux CD32X disc, made from
SuperTux's own title pictures (so the repository carries none of them).

    python3 tools/make_stx_splash.py <SuperTux 0.1.3 folder>/data build/Splash.png
    SPLASH_PLAIN=1 ...   the title picture only (no "LOADING", no panel): the bar alone over it

320x200: SuperTux 0.1.3's title background (images/title/background.jpg) with its logo
(images/title/logo.png), as its title screen, and "LOADING" in SuperTux's gold letters in a
panel around the progress bar - the loading screen the other CD32X discs have (Hurrican's
framed bar, Kobo's "LOADING..." under its bar). The same picture shows at power-on and while a
level pack loads (the loader and the platform layer both show IMAGE.RAW).

The progress bar is drawn by the boot path (cd32x/sh2/cdloader_sh2.c splash_bar,
port/plat_mars.c - not changed): x 56-236 of the 320, lines 186-198 of the 224 (174-186 of
this 200-line picture); the logo sits above it. tools/make_boot_image.py turns the picture into
IMAGE.RAW; tools/splash_bar_colours.py then gives the bar SuperTux's colours.
(Hurrican CD32X's tools/make_splash.py, with SuperTux's pictures.)"""
import os
import sys
from PIL import Image

data, out = sys.argv[1], sys.argv[2]
title = os.path.join(data, "images", "title")
CX, BAR_Y0 = 146, 174

img = Image.open(os.path.join(title, "background.jpg")).convert("RGBA").resize((320, 240), Image.LANCZOS)
img = img.crop((0, 20, 320, 220))

# the logo, its visible part centred on the bar and fitted above "LOADING"
logo = Image.open(os.path.join(title, "logo.png")).convert("RGBA")
logo = logo.resize((logo.width * 13 // 20, logo.height * 13 // 20), Image.LANCZOS)
bx0, by0, bx1, by1 = logo.getchannel("A").point(lambda v: 255 if v >= 128 else 0).getbbox()
img.alpha_composite(logo, (CX - (bx0 + bx1) // 2, 4 - by0))

# SPLASH_PLAIN=1: the title picture alone - the boot path's bar is drawn over it as it is
PLAIN = os.environ.get("SPLASH_PLAIN") == "1"

# a dark glass panel behind "LOADING" and the bar (as the other discs' loading screens: the bar
# sits in a frame of the game's own look), with a light edge
panel = Image.new("RGBA", (200, 48), (8, 16, 48, 150))
edge = Image.new("RGBA", (200, 48), (0, 0, 0, 0))
for x in range(200):
    edge.putpixel((x, 0), (220, 240, 255, 200)); edge.putpixel((x, 47), (220, 240, 255, 200))
for y in range(48):
    edge.putpixel((0, y), (220, 240, 255, 200)); edge.putpixel((199, y), (220, 240, 255, 200))
if not PLAIN:
    img.alpha_composite(panel, (CX - 100, BAR_Y0 - 30))
    img.alpha_composite(edge, (CX - 100, BAR_Y0 - 30))

# "LOADING" in SuperTux's own gold letters (images/status/letters-gold.png: 16 x 18 cells,
# rows ' '-'/', '0'-'?', '@'-'O', 'P'-'_' - src/text.cpp)
font = Image.open(os.path.join(data, "images", "status", "letters-gold.png")).convert("RGBA")
text = "" if PLAIN else "LOADING"
x = CX - len(text) * 16 // 2
for ch in text:
    k = ord(ch) - 32
    cell = font.crop(((k % 16) * 16, (k // 16) * 18, (k % 16) * 16 + 16, (k // 16) * 18 + 18))
    img.alpha_composite(cell, (x, BAR_Y0 - 25))
    x += 16

# make_boot_image.py cuts away black borders (values <= 8): a frame just above that keeps the
# whole 320x200 in place without a visible line
out_img = img.convert("RGB")
for i in range(320):
    out_img.putpixel((i, 0), (9, 9, 9)); out_img.putpixel((i, 199), (9, 9, 9))
for j in range(200):
    out_img.putpixel((0, j), (9, 9, 9)); out_img.putpixel((319, j), (9, 9, 9))
os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
out_img.save(out)
print("  %s: the splash (SuperTux's title background and logo)" % out)
