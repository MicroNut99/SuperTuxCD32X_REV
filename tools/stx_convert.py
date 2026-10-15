#!/usr/bin/env python3
"""stx_convert.py - SuperTux 0.1.3 ("Milestone 1") for the CD32X: all of its data in 32X form.

    python3 tools/stx_convert.py <SuperTux 0.1.3 folder> <out folder> [--previews <folder>]

<SuperTux 0.1.3 folder> is the source tree of tag v0.1.3 (tools/stx_fetch.sh gets it); its data/
folder is read. Nothing of SuperTux is stored in this repository.

SCALE (R2): everything is drawn at HALF of SuperTux's size, SHARPENED (Micronut99 chose
"Sharpened" from four pictures: half, half sharpened, 3/4, full): 640x480 -> 320x240, 32x32
tiles -> 16x16, so the 32X view (320 x 176) is SuperTux's whole width. Pictures are made
smaller premultiplied (no dark fringes) with Lanczos, then an unsharp mask (SHARPEN) brings
back the edges that halving softens (R5: not on the backgrounds - SHARPEN_BACKGROUNDS); the 32X draws keyed, so a pixel is visible when its alpha
is at least 50 %.

Colours (256):
    0          transparent in tiles and sprites; the backdrop on screen
    1-191      the global colours: every tile and every sprite (one palette for the game)
    192-255    a level's background (its picture or gradient), or - on the world map - the
               world map's tiles, or - on the menu pages - their picture: exchanged per screen

Files written (big-endian):
  PALETTE.BIN  192 x u16 32X colours (0BBBBBGGGGGRRRRR), entries 0-191
  TILES.BIN    "STXT" nframes u16, ntiles u16; ntiles x {first frame u16, frames u16,
               anim speed ms u16, flags u16, data u16, next tile u16}
                 flags: 1 solid, 2 ice, 4 water, 8 brick, 16 fullbox (bonus block), 32 distro
                 (coin), 64 coin, 128 goal, 256 spike, 512 unisolid
                 data: fullbox 1 coin / 2 grow-up or ice flower / 3 herring / 4 1up; brick with
                 coins > 0; next tile: what the tile becomes when emptied / broken / taken
               nframes x 16x16 pixels
  SPRITES.BIN  "STXT" nsprites u16, nframes u16; nsprites x {name[24], frames u16, fps x 10 u16,
               x-hotspot s16, y-hotspot s16 (SuperTux's 640x480 units), first frame u16, pad u16};
               nframes x {w u16 (a multiple of 8), h u16, offset u32, dx s16, dy s16}; pixels
               (4-byte aligned). R5: each frame is cut to its visible pixels; (dx, dy) is where
               the cut picture sits in the uncut one, in pixels (dx even) - "STXS" had no cut
               - every sprite of supertux.strf plus EXTRA_SPRITES (logo, world map Tux, level
               dots, snow, cloud, ...)
  FONTS.BIN    SuperTux's letters (write_fonts)
  PICS.BIN     "STXP" n u16, pad u16; n x {name[16], offset u32}; each: 64 colours (192-255),
               320 x 176 pixels - the menu pages' pictures (title background, extro, oiltux)
  WMTILES.BIN  "STXW" ntiles u16, pad u16; 64 colours (192-255); ntiles x {flags u16, pad u16}
               (1 north, 2 south, 4 east, 8 west, 16 stop, 32 auto-walk; one-way << 8:
               1 north-south, 2 south-north, 3 east-west, 4 west-east); ntiles x 16x16 pixels
  WORLDS.BIN   "STXM" nmaps u16, pad u16; nmaps x offset u32; each map: name[32], music track
               u16, start x u16, start y u16, w u16, h u16, nlevels u16, tile ids w*h x u16,
               nlevels x {level u16 (index in LEVELS.TXT, 0xFFFF none), x u16, y u16,
               flags u16 (1 has extro, 2 teleporter, 4 invisible, 8 auto-path, 16 passive message,
               32..256 apply-action north/south/east/west), dest x s16, dest y s16,
               extro u16 (0 extro.txt, 1 extro-bonus.txt, 2 extro-bonus2.txt),
               message[48] (map-message or teleport-message)}
  BACKS.BIN    "STXB" n u16; n x {name[16], w u16, h u16, offset u32}; pictures 320x240,
               64 colours each (192-255) + their 64 palette entries (u16)
  Lnnn.BIN     one per level (nnn = index in LEVELS.TXT):
               "STXL" w u16 h u16, background u16 (0xFFFF: gradient), gradient top u16 bottom u16,
               music track u16, time u16, gravity u16, particles u8 (1 snow, 2 clouds),
               back scrolling u8, start x s16 y s16, nobjects u16, autoscroll u16 (x 256),
               bkgd speed u16; 3 layers (background, interactive, foreground) x w*h u16;
               objects nobjects x {kind u8, stay-on-platform u8, x s16, y s16};
               reset points: n u16, n x {x s16, y s16} (where Tux starts again after a life)
  LEVELS.TXT   index, set, file, name, size;   NAMES.TXT  index, name, author (TAB separated)
  INTRO.TXT, EXTRO.TXT, EXTRO1.TXT, EXTRO2.TXT, CREDITS.TXT  SuperTux's texts
  REPORT.TXT   the measurements
"""
import os
import re
import struct
import sys
import zlib

import numpy as np
from PIL import Image, ImageFilter

SCALE_N, SCALE_D = 1, 2
TILE = 32 * SCALE_N // SCALE_D            # 16
# the unsharp mask after halving (PIL ImageFilter.UnsharpMask: radius, percent, threshold)
SHARPEN = (1.0, 110, 2)
# R5: Micronut99 preferred the backgrounds as they were ("let go back to the original
# backgrounds. I dont like the sharp"): the level backgrounds and the menu pages' pictures are
# halved with Lanczos only. Tiles, sprites and the logo stay sharpened. True brings R2-R4 back.
SHARPEN_BACKGROUNDS = False
GLOBAL_COLOURS = 191       # 1..191
BACK_FIRST = 192
BACK_COLOURS = 64          # 192..255
VIEW_W, VIEW_H = 320, 176
# badguy kinds as game/stx.h K_* (SuperTux's badguy.cpp BadGuyKind order, with its old names)
OBJ_KINDS = {"mriceblock": 0, "laptop": 0, "jumpy": 1, "money": 1, "mrbomb": 2, "stalactite": 4,
             "flame": 5, "fish": 6, "bouncingsnowball": 7, "flyingsnowball": 8, "spiky": 9,
             "snowball": 10, "bsod": 10}
FLAG_NAMES = [("solid", 1), ("ice", 2), ("water", 4), ("brick", 8), ("fullbox", 16), ("distro", 32),
              ("coin", 64), ("goal", 128), ("spike", 256), ("unisolid", 512)]
# CD audio tracks (1 is the data track); tools/stx_music.sh renders them in this order.
MUSIC = ["theme", "Mortimers_chipdisko", "Mortimers_chipdisko-fast", "cave", "cave-fast",
         "fortress", "fortress-fast", "fortress2", "fortress2-fast", "SALCON", "SALCON-fast",
         "leveldone"]
MUSIC_TRACK = {m.lower(): 2 + i for i, m in enumerate(MUSIC)}
LEVEL_SETS = ["world1", "bonus1", "bonus2", "misc"]
WORLDMAPS = ["world1.stwm", "bonusisland1.stwm", "bonusisland2.stwm"]
EXTROS = ["extro.txt", "extro-bonus.txt", "extro-bonus2.txt"]
# sprites that are not in supertux.strf: (name, [pictures under data/images], fps, x-hot, y-hot,
# size factor (1 = the game's half size))
EXTRA_SPRITES = [
    ("logo", ["title/logo.png"], 10, 0, 0, 0.8),             # 128 x 79: five menu rows fit below
    ("wm-smalltux", ["worldmap/smalltux.png"], 10, 0, 10, 1),
    ("wm-tux", ["worldmap/tux.png"], 10, 0, 10, 1),
    ("wm-firetux", ["worldmap/firetux.png"], 10, 0, 10, 1),
    ("leveldot-red", ["worldmap/leveldot_red.png"], 10, 0, 0, 1),
    ("leveldot-green", ["worldmap/leveldot_green.png"], 10, 0, 0, 1),
    ("teleporter", ["worldmap/teleporter.png"], 10, 0, 0, 1),
    ("snow0", ["shared/snow0.png"], 10, 0, 0, 1),
    ("snow1", ["shared/snow1.png"], 10, 0, 0, 1),
    ("snow2", ["shared/snow2.png"], 10, 0, 0, 1),
    ("cloud", ["shared/cloud.png"], 10, 0, 0, 1),
    ("tux-life", ["shared/tux-life.png"], 10, 0, 0, 1),
]
PICS = [("menu", "title/background.jpg"), ("extro", "background/extro.jpg"), ("credits", "background/oiltux.jpg")]
# SuperTux's letters (file, cell w, cell h): FONTS.BIN keeps this order (game/stx.h F_*)
FONTS = [("letters-gold.png", 16, 18), ("letters-white.png", 16, 18), ("letters-white-small.png", 8, 9)]


def sexp(text):
    text = re.sub(r";[^\n]*", "", text)
    toks = re.findall(r'\(|\)|"[^"]*"|[^\s()]+', text)
    out, stack = [], []
    cur = out
    for t in toks:
        if t == "(":
            stack.append(cur)
            cur.append([])
            cur = cur[-1]
        elif t == ")":
            cur = stack.pop()
        else:
            cur.append(t[1:-1] if t.startswith('"') else t)
    return out


def find(lst, key):
    for e in lst:
        if isinstance(e, list) and e and e[0] == key:
            return e
    return None


def val(lst, key, default=None):
    e = find(lst, key)
    return e[1] if e and len(e) > 1 else default


def num(lst, key, default=0):
    """a number from a level file (some write 10.0 where others write 10)"""
    v = val(lst, key, default)
    try:
        return int(round(float(v)))
    except (TypeError, ValueError):
        return default


def fnum(lst, key, default=0.0):
    v = val(lst, key, default)
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


def c555(r, g, b):
    return ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)


def sharpen(im):
    """the unsharp mask of the "Sharpened" choice (RGB picture)"""
    r, pc, th = SHARPEN
    return im.filter(ImageFilter.UnsharpMask(radius=r, percent=pc, threshold=th))


def shrink(im, factor=1.0):
    """RGBA picture -> half size (times factor), sharpened, alpha cut at 50 % (premultiplied: no
    dark fringes)."""
    im = im.convert("RGBA")
    f = SCALE_N / SCALE_D * factor
    w, h = max(1, int(round(im.width * f))), max(1, int(round(im.height * f)))
    pm = im.convert("RGBa").resize((w, h), Image.LANCZOS).convert("RGBA")
    a = np.array(pm)
    a[..., :3] = np.array(sharpen(Image.fromarray(a[..., :3].copy(), "RGB")))
    a[..., 3] = np.where(a[..., 3] >= 128, 255, 0)
    return a


class Palette:
    def __init__(self, samples, n=GLOBAL_COLOURS, first=1):
        """samples: list of RGBA arrays; builds n colours from their opaque pixels."""
        px = np.concatenate([s[s[..., 3] == 255][:, :3] for s in samples if (s[..., 3] == 255).any()])
        img = Image.fromarray(px.reshape(1, -1, 3).astype(np.uint8), "RGB")
        q = img.quantize(colors=n, method=Image.MEDIANCUT, dither=Image.NONE)
        pal = np.array(q.getpalette()[:n * 3], dtype=np.int32).reshape(-1, 3)
        self.rgb = np.zeros((256, 3), dtype=np.int32)
        self.rgb[first:first + len(pal)] = pal
        self.first, self.count = first, len(pal)

    def index(self, a):
        """RGBA array -> indexed (0 = transparent), nearest colour."""
        rgb = a[..., :3].astype(np.int32).reshape(-1, 3)
        pal = self.rgb[self.first:self.first + self.count]
        out = np.zeros(len(rgb), dtype=np.uint8)
        for s in range(0, len(rgb), 4096):
            d = ((rgb[s:s + 4096, None, :] - pal[None, :, :]) ** 2).sum(-1)
            out[s:s + 4096] = d.argmin(1) + self.first
        out[a[..., 3].reshape(-1) == 0] = 0
        return out.reshape(a.shape[:2])

    def words(self, n):
        return b"".join(struct.pack(">H", c555(int(r), int(g), int(b)) or 0x0400)
                        for r, g, b in self.rgb[self.first:self.first + n])


def picture64(im):
    """an RGB picture in 64 colours for palette entries 192-255: (pixels, 64 colour words)"""
    q = im.convert("RGB").quantize(colors=BACK_COLOURS, method=Image.MEDIANCUT, dither=Image.NONE)
    px = (np.array(q, dtype=np.uint16) + BACK_FIRST).astype(np.uint8)
    pal = np.array(q.getpalette()[:BACK_COLOURS * 3]).reshape(-1, 3)
    pal = np.vstack([pal, np.zeros((BACK_COLOURS - len(pal), 3), dtype=pal.dtype)])
    return px, b"".join(struct.pack(">H", c555(int(r), int(g), int(b)) or 0x0400) for r, g, b in pal)


def padded(idx):
    """an indexed picture padded on the right to a multiple of 8 (YATSSD's loops)"""
    h, w = idx.shape
    p = np.zeros((h, (w + 7) & ~7), dtype=np.uint8)
    p[:, :w] = idx
    return p


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    src, outdir = sys.argv[1], sys.argv[2]
    prevdir = sys.argv[sys.argv.index("--previews") + 1] if "--previews" in sys.argv else None
    D = os.path.join(src, "data")
    if not os.path.exists(os.path.join(D, "images/tilesets/supertux.stgt")):
        sys.exit("*** no SuperTux 0.1.3 data in %s (tools/stx_fetch.sh gets it)" % src)
    os.makedirs(outdir, exist_ok=True)
    rep = []

    # ---- tile set ----
    ts = sexp(open(os.path.join(D, "images/tilesets/supertux.stgt")).read())[0]
    tiles = {}
    for e in ts[1:]:
        if isinstance(e, list) and e and e[0] == "tile":
            tid = int(val(e, "id"))
            ims = find(e, "images")
            flags = 0
            for name, bit in FLAG_NAMES:
                if val(e, name) == "#t":
                    flags |= bit
            tiles[tid] = {"images": ims[1:] if ims else [], "flags": flags,
                          "speed": num(e, "anim-speed", 25), "data": num(e, "data", 0),
                          "next": num(e, "next-tile", 0)}
    raw_tiles = {}
    for tid in sorted(tiles):
        arrs = []
        for nm in tiles[tid]["images"]:
            p = os.path.join(D, "images/tilesets", nm)
            if os.path.exists(p):
                arrs.append(shrink(Image.open(p)))
        raw_tiles[tid] = arrs

    # ---- sprites ----
    res = sexp(open(os.path.join(D, "supertux.strf")).read())[0]
    sprite_defs, seen = [], set()
    for e in res[1:]:
        if isinstance(e, list) and e and e[0] == "sprite":
            name = val(e, "name")
            if name in seen:
                continue
            seen.add(name)
            ims = find(e, "images")
            sprite_defs.append((name, ["shared/" + os.path.basename(i) if not i.startswith("shared/") else i for i in (ims[1:] if ims else [])],
                                float(val(e, "fps", 10)), num(e, "x-hotspot"), num(e, "y-hotspot"), 1))
    sprite_defs += EXTRA_SPRITES
    sprite_arrs = []
    for name, ims, fps, hx, hy, fac in sprite_defs:
        sprite_arrs.append([shrink(Image.open(os.path.join(D, "images", i)), fac) for i in ims])

    pal = Palette([a for v in raw_tiles.values() for a in v] + [a for v in sprite_arrs for a in v])
    ntiles = max(tiles) + 1
    tile_px, tdesc = [], []
    for tid in range(ntiles):
        t = tiles.get(tid)
        first = len(tile_px)
        for a in raw_tiles.get(tid, []):
            fr = np.zeros((TILE, TILE), dtype=np.uint8)
            ia = pal.index(a)
            fr[:min(TILE, ia.shape[0]), :min(TILE, ia.shape[1])] = ia[:TILE, :TILE]
            tile_px.append(fr)
        tdesc.append((first, len(tile_px) - first, t["speed"] if t else 0, t["flags"] if t else 0,
                      t["data"] if t else 0, t["next"] if t else 0))
    with open(os.path.join(outdir, "TILES.BIN"), "wb") as f:
        f.write(b"STXT" + struct.pack(">HH", len(tile_px), ntiles))
        for d in tdesc:
            f.write(struct.pack(">HHHHHH", *d))
        for fr in tile_px:
            f.write(fr.tobytes())
    with open(os.path.join(outdir, "PALETTE.BIN"), "wb") as f:
        f.write(struct.pack(">H", 0x0400) + pal.words(GLOBAL_COLOURS))   # entries 0 (unused) and 1-191
    spr_saved = write_sprites(outdir, pal, sprite_defs, sprite_arrs)
    write_fonts(D, outdir, pal)
    write_pics(D, outdir)
    rep.append("scale 1/2 sharpened: tiles %d ids, %d frames of %dx%d = %d bytes; global colours used: %d"
               % (ntiles, len(tile_px), TILE, TILE, len(tile_px) * TILE * TILE, pal.count))
    rep.append("sprites: %d (%d pictures), cut to their visible pixels: %d -> %d bytes"
               % (len(sprite_defs), sum(len(v) for v in sprite_arrs), spr_saved[0], spr_saved[1]))
    for t, o in (("intro.txt", "INTRO.TXT"), ("extro.txt", "EXTRO.TXT"), ("extro-bonus.txt", "EXTRO1.TXT"),
                 ("extro-bonus2.txt", "EXTRO2.TXT"), ("CREDITS", "CREDITS.TXT")):
        p = os.path.join(D, t)
        txt = open(p, "rb").read().replace(b"\r", b"") if os.path.exists(p) else b""
        open(os.path.join(outdir, o), "wb").write(txt)

    # ---- backgrounds ----
    backs, back_index = [], {}
    bw, bh = 640 * SCALE_N // SCALE_D, 480 * SCALE_N // SCALE_D
    for nm in sorted(os.listdir(os.path.join(D, "images/background"))):
        im = Image.open(os.path.join(D, "images/background", nm)).convert("RGB").resize((bw, bh), Image.LANCZOS)
        if SHARPEN_BACKGROUNDS:
            im = sharpen(im)
        px, words = picture64(im)
        back_index[nm] = len(backs)
        backs.append((nm, px, words))
    with open(os.path.join(outdir, "BACKS.BIN"), "wb") as f:
        hdr = 6 + len(backs) * 24
        f.write(b"STXB" + struct.pack(">H", len(backs)))
        off = hdr
        for nm, px, words in backs:
            f.write(nm.encode()[:16].ljust(16, b"\0") + struct.pack(">HHI", bw, bh, off))
            off += px.size + BACK_COLOURS * 2
        for nm, px, words in backs:
            f.write(px.tobytes() + words)
    rep.append("backgrounds: %d pictures, %dx%d in 64 colours" % (len(backs), bw, bh))

    # ---- levels ----
    lvdir = os.path.join(D, "levels")
    files = []
    for s in LEVEL_SETS:
        p = os.path.join(lvdir, s)
        if os.path.isdir(p):
            names = sorted((n for n in os.listdir(p) if n.endswith(".stl")),
                           key=lambda n: [int(x) if x.isdigit() else x for x in re.split(r"(\d+)", n)])
            files += [(s, n) for n in names]
    level_index = {"%s/%s" % sn: i for i, sn in enumerate(files)}
    listing, names_txt, total_map, total_z = [], [], 0, 0
    kinds_seen, music_used = {}, {}
    previews = []
    for i, (s, n) in enumerate(files):
        lv = sexp(open(os.path.join(lvdir, s, n), errors="replace").read())[0]
        W, H = num(lv, "width", 0), num(lv, "height", 15)
        layers = []
        for nm in ("background-tm", "interactive-tm", "foreground-tm"):
            e = find(lv, nm)
            v = [int(x) for x in e[1:]] if e else []
            v = (v + [0] * (W * H))[:W * H]
            layers.append(np.array(v, dtype=np.uint16).reshape(H, W))
        bg = val(lv, "background", "")
        bgi = back_index.get(bg, 0xFFFF) if bg else 0xFFFF
        top = c555(num(lv, "bkgd_red_top"), num(lv, "bkgd_green_top"), num(lv, "bkgd_blue_top"))
        bot = c555(num(lv, "bkgd_red_bottom"), num(lv, "bkgd_green_bottom"), num(lv, "bkgd_blue_bottom"))
        mus = val(lv, "music", "")
        track = MUSIC_TRACK.get(os.path.splitext(mus)[0].lower(), 0)
        music_used[mus] = track
        objs = []
        oe = find(lv, "objects")
        for o in (oe[1:] if oe else []):
            if isinstance(o, list) and o:
                k = o[0]
                kinds_seen[k] = kinds_seen.get(k, 0) + 1
                if k in OBJ_KINDS:
                    objs.append((OBJ_KINDS[k], 1 if val(o, "stay-on-platform") == "#t" else 0, num(o, "x"), num(o, "y")))
        sx, sy = num(lv, "start_pos_x", 100), num(lv, "start_pos_y", 170)
        part = {"": 0, "snow": 1, "clouds": 2}.get(val(lv, "particle_system", ""), 0)
        back_scroll = 1 if val(lv, "back_scrolling") == "#t" else 0
        autoscroll = int(round(fnum(lv, "hor_autoscroll_speed", 0.0) * 256))
        bspeed = num(lv, "bkgd_speed", 50)
        body = b"STXL" + struct.pack(">HHHHHHHHBBhhHHH", W, H, bgi, top, bot, track,
                                     num(lv, "time", 300), num(lv, "gravity", 10), part, back_scroll,
                                     sx, sy, len(objs), autoscroll, bspeed)
        body += b"".join(l.astype(">u2").tobytes() for l in layers)
        body += b"".join(struct.pack(">BBhh", k, st, x, y) for k, st, x, y in objs)
        rp = find(lv, "reset-points")
        pts = [(num(pt, "x"), num(pt, "y")) for pt in (rp[1:] if rp else []) if isinstance(pt, list)]
        body += struct.pack(">H", len(pts)) + b"".join(struct.pack(">hh", x, y) for x, y in pts)
        with open(os.path.join(outdir, "L%03d.BIN" % i), "wb") as f:
            f.write(body)
        z = len(zlib.compress(body, 9))
        total_map += len(body)
        total_z += z
        name = val(lv, "name", n)
        author = val(lv, "author", "")
        listing.append("%3d  %-7s %-24s %-40s %4dx%-3d %7d bytes (%6d packed)"
                       % (i, s, n, name[:40], W, H, len(body), z))
        names_txt.append("%d\t%s\t%s" % (i, name, author))
        if prevdir and s == "world1":
            previews.append((i, s, n, render(layers, tdesc, tile_px, pal, backs, bgi, top, bot, W, H)))
    with open(os.path.join(outdir, "LEVELS.TXT"), "w") as f:
        f.write("\n".join(listing) + "\n")
    with open(os.path.join(outdir, "NAMES.TXT"), "w") as f:
        f.write("\n".join(names_txt) + "\n")
    rep.append("levels: %d (%s); %d bytes as stored, %d compressed (zlib, as a measure)"
               % (len(files), ", ".join(LEVEL_SETS), total_map, total_z))
    rep.append("music used by levels: " + ", ".join("%s -> track %d" % (m or "(none)", t)
               for m, t in sorted(music_used.items(), key=lambda x: x[1])))
    rep.append("objects: " + ", ".join("%s %d" % kv for kv in sorted(kinds_seen.items(), key=lambda x: -x[1])))

    # ---- the world maps ----
    write_worldmaps(D, outdir, level_index, rep)

    with open(os.path.join(outdir, "REPORT.TXT"), "w") as f:
        f.write("\n".join(rep) + "\n")
    print("\n".join(rep))
    if prevdir:
        os.makedirs(prevdir, exist_ok=True)
        for i, s, n, img in previews:
            img.save(os.path.join(prevdir, "L%03d_%s_%s.png" % (i, s, n[:-4])))
        print("previews: %d levels -> %s" % (len(previews), prevdir))


def trimmed(idx, name):
    """R5: a sprite picture cut down to its visible pixels (colour 0 is see-through), with the
    cut-off left / top as an offset. Every cell a sprite touches is drawn again by the tiler in
    the next frames, so the transparent margins cost real time: SuperTux's 32x32 snowflakes hold
    6x5 to 9x8 visible pixels at half size and covered up to 4 cells each. The left offset is kept
    even: yt_sprite rounds x down to even, so the pixels land exactly where the uncut picture put
    them. The logo is not cut (the title centres it by its width)."""
    if name == "logo" or os.environ.get("STX_NOTRIM"):   # STX_NOTRIM=1: uncut (to compare)
        return idx, 0, 0
    ys, xs = np.nonzero(idx)
    if len(xs) == 0:
        return np.zeros((2, 8), dtype=np.uint8), 0, 0
    x0, y0 = int(xs.min()) & ~1, int(ys.min())
    return idx[y0:int(ys.max()) + 1, x0:int(xs.max()) + 1], x0, y0


def write_sprites(outdir, pal, defs, arrs):
    """SPRITES.BIN (formats above). Every picture cut to its visible pixels (trimmed), padded to a
    width that is a multiple of 8 and 4-byte aligned: YATSSD's 16-bit loop copies 32 bits at a
    time and never gets fewer than 4 units (its counter would wrap - Sonic32 T7)."""
    entries, frames, pix = [], [], []
    saved = [0, 0]
    for (name, ims, fps, hx, hy, fac), a in zip(defs, arrs):
        first = len(frames)
        for im in a:
            full = pal.index(im)
            cut, dx, dy = trimmed(full, name)
            p = padded(cut)
            saved[0] += padded(full).size
            saved[1] += p.size
            frames.append((p.shape[1], p.shape[0], dx, dy))
            pix.append(p.tobytes())
        entries.append((name, len(a), int(round(fps * 10)), hx, hy, first))
    head = 8 + 36 * len(entries) + 12 * len(frames)
    out = bytearray(b"STXT" + struct.pack(">HH", len(entries), len(frames)))
    for name, n, fps10, hx, hy, first in entries:
        out += name.encode()[:24].ljust(24, b"\0") + struct.pack(">HHhhHH", n, fps10, hx, hy, first, 0)
    off = (head + 3) & ~3
    offs = []
    for p in pix:
        offs.append(off)
        off = (off + len(p) + 3) & ~3
    for (w, h, dx, dy), o in zip(frames, offs):
        out += struct.pack(">HHIhh", w, h, o, dx, dy)
    for p, o in zip(pix, offs):
        out += b"\0" * (o - len(out)) + p
    out += b"\0" * ((-len(out)) & 3)
    with open(os.path.join(outdir, "SPRITES.BIN"), "wb") as f:
        f.write(out)
    return saved


def write_fonts(D, outdir, pal):
    """FONTS.BIN: SuperTux's own letters at their own size (the menus' text is screen text, not
    the game's world), images/status/letters-*.png, src/text.cpp: 16 cells a row, rows ' '-'/',
    '0'-'?', '@'-'O', 'P'-'_', '`'-'o', 'p'-DEL; in the global colours, padded to a multiple of 8.
      "STXF" n u16, pad u16; n x {cell w u16 (padded), h u16, advance u16, pad u16, offset u32};
      then per font 96 cells of w*h bytes (characters 32-127)"""
    fonts = []
    for name, cw, chh in FONTS:
        im = Image.open(os.path.join(D, "images", "status", name)).convert("RGBA")
        cells = []
        for k in range(96):
            c = np.array(im.crop(((k % 16) * cw, (k // 16) * chh, (k % 16) * cw + cw, (k // 16) * chh + chh)))
            c[..., 3] = np.where(c[..., 3] >= 128, 255, 0)
            cells.append(padded(pal.index(c)))
        h, pw = cells[0].shape
        fonts.append((pw, h, cw, b"".join(c.tobytes() for c in cells)))
    out = bytearray(b"STXF" + struct.pack(">HH", len(fonts), 0))
    off = 8 + 12 * len(fonts)
    for pw, h, w, data in fonts:
        out += struct.pack(">HHHHI", pw, h, w, 0, off)
        off += (len(data) + 3) & ~3
    for pw, h, w, data in fonts:
        out += data + b"\0" * ((-len(data)) & 3)
    with open(os.path.join(outdir, "FONTS.BIN"), "wb") as f:
        f.write(out)


def write_pics(D, outdir):
    """PICS.BIN: the menu pages' pictures, 320 x 176 (the view), 64 colours each (192-255)"""
    items = []
    for name, path in PICS:
        im = Image.open(os.path.join(D, "images", path)).convert("RGB").resize((320, 240), Image.LANCZOS)
        if SHARPEN_BACKGROUNDS:
            im = sharpen(im)
        im = im.crop((0, (240 - VIEW_H) // 2, 320, (240 - VIEW_H) // 2 + VIEW_H))
        px, words = picture64(im)
        items.append((name, words + px.tobytes()))
    out = bytearray(b"STXP" + struct.pack(">HH", len(items), 0))
    off = 8 + 20 * len(items)
    for name, data in items:
        out += name.encode()[:16].ljust(16, b"\0") + struct.pack(">I", off)
        off += (len(data) + 3) & ~3
    for name, data in items:
        out += data + b"\0" * ((-len(data)) & 3)
    with open(os.path.join(outdir, "PICS.BIN"), "wb") as f:
        f.write(out)


def write_worldmaps(D, outdir, level_index, rep):
    """WMTILES.BIN and WORLDS.BIN (formats above): images/worldmap/antarctica.stwt and the three
    world maps of data/levels/worldmaps, at half size (16-pixel tiles), in their own 64 colours."""
    st = sexp(open(os.path.join(D, "images/worldmap/antarctica.stwt")).read())[0]
    tdefs = {}
    for e in st[1:]:
        if isinstance(e, list) and e and e[0] == "tile":
            tid = int(val(e, "id"))
            fl = 0
            for k, bit in (("north", 1), ("south", 2), ("east", 4), ("west", 8), ("stop", 16)):
                if val(e, k, "#t") == "#t":
                    fl |= bit
            if val(e, "auto-walk", "#f") == "#t":
                fl |= 32
            ow = {"north-south": 1, "south-north": 2, "east-west": 3, "west-east": 4}.get(val(e, "one-way", ""), 0)
            fl |= ow << 8
            tdefs[tid] = (fl, val(e, "image"))
    n = max(tdefs) + 1
    imgs = []
    for tid in range(n):
        fl, img = tdefs.get(tid, (0, None))
        a = shrink(Image.open(os.path.join(D, "images/worldmap", img))) if img else np.zeros((TILE, TILE, 4), np.uint8)
        imgs.append(a)
    pal = Palette(imgs, n=BACK_COLOURS, first=BACK_FIRST)
    out = bytearray(b"STXW" + struct.pack(">HH", n, 0)) + pal.words(BACK_COLOURS)
    for tid in range(n):
        out += struct.pack(">HH", tdefs.get(tid, (0, None))[0], 0)
    for a in imgs:
        fr = np.zeros((TILE, TILE), dtype=np.uint8)
        ia = pal.index(a)
        fr[:min(TILE, ia.shape[0]), :min(TILE, ia.shape[1])] = ia[:TILE, :TILE]
        out += fr.tobytes()
    with open(os.path.join(outdir, "WMTILES.BIN"), "wb") as f:
        f.write(out)

    maps = []
    for mf in WORLDMAPS:
        wm = sexp(open(os.path.join(D, "levels/worldmaps", mf)).read())[0]
        props, tm, lvls = find(wm, "properties"), find(wm, "tilemap"), find(wm, "levels")
        w, h = num(tm, "width"), num(tm, "height")
        data = [int(x) for x in find(tm, "data")[1:]]
        data = (data + [0] * (w * h))[:w * h]
        mus = val(props, "music", "")
        track = MUSIC_TRACK.get(os.path.splitext(mus)[0].lower(), MUSIC_TRACK["theme"]) if mus else MUSIC_TRACK["theme"]
        body = (val(props, "name", mf).encode("ascii", "replace")[:31].ljust(32, b"\0")
                + struct.pack(">HHHHHH", track, num(props, "start_pos_x"), num(props, "start_pos_y"), w, h,
                              len([l for l in lvls[1:] if isinstance(l, list)])))
        body += struct.pack(">%dH" % (w * h), *data)
        for l in lvls[1:]:
            if not isinstance(l, list):
                continue
            nm = val(l, "name", "")
            li = level_index.get(os.path.splitext(nm)[0] + ".stl", 0xFFFF) if nm else 0xFFFF
            fl = 0
            ex = val(l, "extro-filename", "")
            if ex:
                fl |= 1
            if val(l, "dest_x") is not None:
                fl |= 2
            if val(l, "invisible-teleporter") == "#t":
                fl |= 4
            if val(l, "auto-path", "#t") == "#t":
                fl |= 8
            if val(l, "passive-message", "#t") == "#t":
                fl |= 16
            for k, bit in (("apply-action-up", 32), ("apply-action-down", 64), ("apply-action-right", 128), ("apply-action-left", 256)):
                if val(l, k, "#t") == "#t":
                    fl |= bit
            msg = val(l, "map-message", "") or val(l, "teleport-message", "")
            body += struct.pack(">HHHHhhH", li, num(l, "x"), num(l, "y"), fl, num(l, "dest_x", -1), num(l, "dest_y", -1),
                                EXTROS.index(ex) if ex in EXTROS else 0)
            body += msg.encode("ascii", "replace")[:47].ljust(48, b"\0")
        maps.append(body)
    out = bytearray(b"STXM" + struct.pack(">HH", len(maps), 0))
    off = 8 + 4 * len(maps)
    for b in maps:
        out += struct.pack(">I", off)
        off += (len(b) + 3) & ~3
    for b in maps:
        out += b + b"\0" * ((-len(b)) & 3)
    with open(os.path.join(outdir, "WORLDS.BIN"), "wb") as f:
        f.write(out)
    rep.append("world maps: %s; %d map tiles" % (", ".join(WORLDMAPS), n))


def render(layers, tdesc, tile_px, pal, backs, bgi, top, bot, W, H):
    """The level's start as the 32X draws it (half size, first animation frames, the view's 176
    lines from the bottom)."""
    full = np.zeros((H * TILE, W * TILE), dtype=np.uint8)
    rgb = pal.rgb.copy()
    if bgi != 0xFFFF:
        nm, px, words = backs[bgi]
        for k in range(BACK_COLOURS):
            c = struct.unpack(">H", words[2 * k:2 * k + 2])[0]
            rgb[BACK_FIRST + k] = ((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3)
        for x0 in range(0, W * TILE, px.shape[1]):
            seg = px[:min(px.shape[0], H * TILE), :min(px.shape[1], W * TILE - x0)]
            full[:seg.shape[0], x0:x0 + seg.shape[1]] = seg
    else:
        def unpack(c):
            return np.array([(c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3])
        t, b = unpack(top), unpack(bot)
        for k in range(BACK_COLOURS):
            rgb[BACK_FIRST + k] = t + (b - t) * k // (BACK_COLOURS - 1)
        for y in range(H * TILE):
            full[y, :] = BACK_FIRST + y * (BACK_COLOURS - 1) // max(1, H * TILE - 1)
    for layer in layers:
        for ty in range(H):
            for tx in range(W):
                t = layer[ty, tx]
                if t and t < len(tdesc) and tdesc[t][1]:
                    fr = tile_px[tdesc[t][0]]
                    dst = full[ty * TILE:(ty + 1) * TILE, tx * TILE:(tx + 1) * TILE]
                    np.copyto(dst, fr, where=fr != 0)
    img = Image.fromarray(rgb.astype(np.uint8)[full], "RGB")
    return img.crop((0, max(0, H * TILE - VIEW_H), min(W * TILE, 3 * VIEW_W), H * TILE))


if __name__ == "__main__":
    main()
