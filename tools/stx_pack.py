#!/usr/bin/env python3
"""stx_pack.py - SuperTux CD32X: the data for the disc, from stx_convert.py's output (R3).

    python3 tools/stx_pack.py build/data cd32x/subcpu/ROMS

THE CART (4 MB, the SH2 reads it; only the 68000 can write it, through the boot path's
commands 38 + 46 - port/plat_mars.c, cd32x/subcpu/main.c, both unchanged):

  0x000000  the program (OPENLARA.32X)
  0x100000  IMAGE8.RAW - read at power-on, under the splash's progress bar: everything that is
            not a level, plus the title's demo level (DEMO.PAK) and HOMES.BIN (below)
  ........  area A: some levels, kept for good once read (until the console is switched off)
  0x280000  area B: the other levels, each at a fixed place ("home") in a ring - reading one
            drops the levels whose homes it overlaps, the rest stay
  0x3F0000  the saves (not ours)

Why homes: command 38 opens a disc file for a FIXED cart slot (IMAGE<n>.RAW -> slot n x 128 KB,
OJ<k>.PAK -> 0x280000) and command 46 copies 32 KB piece c of it to slot + c x 32 KB. So a level
lands where it lies inside its file. Area A is one file, IMAGE9.RAW (slot 9 = 0x120000: zeros up
to area A, then its levels each at their home); area B levels are OJnnn.PAK files with zeros in
front of the level up to its home. The game reads only the pieces that hold the level (zeros are
never read: the disc seeks past them). game/level.c keeps track of what is in the cart, so a
level read once is there at once next time - the title's demo always, area A's always, area B's
until other levels have used their room.

HOMES.BIN (in IMAGE8.RAW): the 77 levels, then (R4) the background pictures, each {area u8
(0 = in IMAGE8, 1 = A, 2 = B), a level's background picture u8 (picture + 1, 0 = none; REV3:
+ 0x80 when it moves at its own speed), first piece u16, pieces u16, disc file u16 (9 =
IMAGE9.RAW, 200 + nnn = OJnnn.PAK), file length u32}. In IMAGE8 (area 0): the demo level as
DEMO.PAK. A background's pack holds BACK.BIN: w u16, h u16, row
length u16 (w + 16), pad u16, then the rows, each followed by its own first 16 pixels again;
REV3: and GENBG.BIN, the same picture for the Genesis' plane B (tools/stx_genesis.py). A level
with a picture also holds LEVELG.BIN + COMPG.BIN: the level again with its pictures see-through
where the background picture shows (colours 192-255 = 0x8000, the 32X's through bit). Piece c of file 9 lands at 0x120000 + c x 32 KB, of OJ files at 0x280000 + c x 32 KB.

A LEVEL (each an OJPK pack, port/pack.h format, two files):

  LEVEL.BIN
     0  "STX3"
     4  w u16, h u16 (tiles); 8 music track u16, time u16 (s), gravity u16
    14  start x s16, start y s16 (SuperTux units); 18 background u16 (0xFFFF gradient)
    20  particles u8 (1 snow, 2 clouds), back scrolling u8; 22 objects u16;
    24  autoscroll u16 (units a step x 256); 26 background speed u16 (%)
    28  resources u16 (R); 30 animated resources u16 (A); 32 variants u16 (V);
    34  picture blocks u16 (P); 36 frame list entries u16 (F); 38 pad u16
    40  the layer the tiler draws: w*h x u16 YATSSD tile numbers (resource << 2)
        the background-tm, interactive-tm (the rules) and foreground-tm: 3 x w*h x u16
        SuperTux tile ids
        R+1 x u16: resource r's first picture block (resource 0 = nothing)
        A x {resource u16, frames u16, ms a frame u16, first entry u16}: animated resources
        F x u16: their frames' picture blocks (A's entries point into this list)
        V x {cell u16, interactive tile id u16, YATSSD tile number u16}: what a cell shows
            when its interactive tile becomes another one (coin taken, box emptied, brick
            broken; tile 0 also while a brick bounces), sorted by cell, then tile
        objects n x {kind u8, stay-on-platform u8, x s16, y s16}; reset points n u16, n x {x, y}
  COMP.BIN  64 x u16 colours (palette 192-255), then P picture blocks of 16x16 bytes

THE MERGED LAYERS (R3): every cell's four layers (the background picture or gradient,
SuperTux's background-tm, interactive-tm and foreground-tm) are merged here into ONE 16x16
picture, identical pictures kept once. Vic's tiler then draws one layer, and its dirty matrix
(one tile number a cell) skips every cell that has not changed - R2's four layers redrew every
cell with a tile over the background every frame (Micronut99's timing line: N 160-240 tiles,
T 23-29 ms). Animated tiles (coins, water, the goal) give animated pictures (at most 6 frames,
identical frames stored once).
The foreground-tm is in the picture too; the game draws its tiles again over a sprite where
one passes (game/game.c), and the other layers over the far snow / clouds.
"""
import os
import struct
import sys

import numpy as np

TILE = 16
CHUNK = 32768
SLOT = 0x20000
IMAGE8_AT = 0x100000
AREA_A_SLOT = 9                        # IMAGE9.RAW -> 0x120000
AREA_A_END = 0x280000
AREA_B_AT = 0x280000                   # OJnnn.PAK -> 0x280000 (main.c: cart slot 20)
AREA_B_END = 0x3F0000                  # the saves begin here
DEMO_LEVEL = 76                        # misc/menu.stl, the title's demo (game/game.c DEMO_LEVEL)
MAX_RES = 2560                         # game/level.c MAX_RES
MAX_FRAMES = 6                         # an animated picture: at most 6 frames
F_BRICK, F_FULLBOX, F_DISTRO = 8, 16, 32


def fail(msg):
    sys.exit("*** stx_pack.py: " + msg)


def make_pack(files):
    """OJPK version 2 with files only (port/pack.h) - the same writer as Hurrican's hur_convert.py"""
    n = len(files)
    pos = 24 + 24 * n
    offs = []
    for name, data in files:
        pos = (pos + 3) & ~3
        offs.append(pos)
        pos += len(data)
    total = (pos + 3) & ~3
    out = bytearray(b"OJPK" + struct.pack(">IIIII", 2, n, 0, total, 0))
    for (name, data), o in zip(files, offs):
        out += name.encode("ascii").ljust(16, b"\0") + struct.pack(">II", o, len(data))
    for (name, data), o in zip(files, offs):
        out += b"\0" * (o - len(out))
        out += data
    out += b"\0" * (total - len(out))
    return bytes(out)


def c555(r, g, b):
    return ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)


def rgb_of(c):
    return ((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3)


def lcm(a, b):
    import math
    return a * b // math.gcd(a, b)


THROUGH = 0x8000                       # REV3: a 32X colour with the through bit (32X manual
                                       # "Priority"): its pixels show the Genesis picture behind


def build_level(lv, tdesc, frames, back, genesis=False):
    """genesis (REV3, OPTIONS - BACKGROUND GENESIS): the level's pictures WITHOUT the background
    picture - where it would show, colour 192 (all of 192-255 become THROUGH colours), so the
    Genesis plane with the picture (GENBG.BIN) shows through there. Returns LEVEL.BIN, COMP.BIN."""
    ntiles = len(tdesc)
    (W, H, bgi, top, bot, track, ltime, grav, part, bscroll, sx, sy, nobj, autos, bspeed) = \
        struct.unpack(">HHHHHHHHBBhhHHH", lv[4:32])
    cells = W * H
    base = 32
    lay = [np.frombuffer(lv[base + 2 * cells * k:base + 2 * cells * (k + 1)], ">u2").astype(np.int32) for k in range(3)]
    for L in lay:
        L[L >= ntiles] = 0
    objs = lv[base + 6 * cells:base + 6 * cells + 6 * nobj]
    o = base + 6 * cells + 6 * nobj
    nres = struct.unpack(">H", lv[o:o + 2])[0]
    resets = lv[o:o + 2 + 4 * nres]

    # the background's 16x16 blocks (picture repeated, or the gradient: a shade a line)
    if genesis:
        bgblocks = [np.full((TILE, TILE), 192, np.uint8)]
        bg_of = lambda tx, ty: 0
        colours = struct.pack(">64H", *([THROUGH] * 64))
    elif bgi != 0xFFFF:
        px, cols, bw, bh = back[bgi]
        pic = np.frombuffer(px, np.uint8).reshape(bh, bw)
        ncol, nrow = bw // TILE, bh // TILE
        bgblocks = [pic[ty * TILE:(ty + 1) * TILE, tx * TILE:(tx + 1) * TILE].copy()
                    for ty in range(nrow) for tx in range(ncol)]
        bg_of = lambda tx, ty: (ty % nrow) * ncol + (tx % ncol)
        colours = cols
    else:
        t, b = rgb_of(top), rgb_of(bot)
        colours = b"".join(struct.pack(">H", c555(*[int(t[c] + (b[c] - t[c]) * s / 63) for c in range(3)]) or 0x0400)
                           for s in range(64))
        bgblocks = []
        for ty in range(H):
            blk = np.zeros((TILE, TILE), np.uint8)
            for y in range(TILE):
                blk[y, :] = 192 + min(63, (ty * TILE + y) * 64 // (H * TILE))
            bgblocks.append(blk)
        bg_of = lambda tx, ty: ty

    def anim(t):
        d = tdesc[t]
        return d[1] > 1 and d[2] > 0

    def tile_frame(t, k):
        d = tdesc[t]
        if t == 0 or d[1] == 0:
            return None
        return frames[d[0] + (k % d[1])]

    blocks = []                        # picture blocks (bytes)
    block_of = {}                      # static picture -> block (identical pictures once)
    res_first = [0]                    # resource -> first block (0: nothing)
    anims = []                         # (resource, frames, ms a frame, first entry in frame_list)
    frame_list = []                    # the blocks of the animated resources' frames
    res_of_key = {}
    multi_speed = 0

    def resource(b, t1, t2, t3):
        nonlocal multi_speed
        key = (b, t1, t2, t3)
        if key in res_of_key:
            return res_of_key[key]
        stack = [t for t in (t1, t2, t3) if t]
        animated = [t for t in stack if anim(t)]
        nf, speed = 1, 0
        if animated:
            speeds = {tdesc[t][2] for t in animated}
            if len(speeds) == 1:
                speed = speeds.pop()
                for t in animated:
                    nf = lcm(nf, tdesc[t][1])
                nf = min(nf, 16)
            else:
                multi_speed += 1
                top_t = animated[-1]   # several speeds: the top-most layer's animation
                speed, nf = tdesc[top_t][2], tdesc[top_t][1]
                animated = [top_t]
        if nf > MAX_FRAMES:            # long animations (water: 11-12 frames) thinned out
            step = (nf + MAX_FRAMES - 1) // MAX_FRAMES
            ks = list(range(0, nf, step))
            speed *= step
        else:
            ks = list(range(nf))
        pics = []
        for k in ks:
            img = bgblocks[b].copy()
            for t in stack:
                f = tile_frame(t, k if t in animated else 0)
                if f is not None:
                    img = np.where(f != 0, f, img)
            pics.append(img.tobytes())

        def block(p):
            if p not in block_of:
                block_of[p] = len(blocks)
                blocks.append(p)
            return block_of[p]
        if len(ks) == 1 or len(set(pics)) == 1:
            first = block(pics[0])
            # a static picture already a resource: share the resource too
            r = res_of_key.get(("pic", first))
            if r is None:
                r = len(res_first)
                res_first.append(first)
                res_of_key[("pic", first)] = r
        else:
            seq = tuple(block(p) for p in pics)
            r = res_of_key.get(("anim", seq, speed))
            if r is None:
                r = len(res_first)
                res_first.append(seq[0])
                anims.append((r, len(seq), speed, len(frame_list)))
                frame_list.extend(seq)
                res_of_key[("anim", seq, speed)] = r
        res_of_key[key] = r
        return r

    comp = np.zeros(cells, np.int32)
    variants = []
    changeable = lambda t: t and (tdesc[t][3] & (F_BRICK | F_FULLBOX | F_DISTRO))
    for ty in range(H):
        for tx in range(W):
            c = ty * W + tx
            b = bg_of(tx, ty)
            t1, t2, t3 = int(lay[0][c]), int(lay[1][c]), int(lay[2][c])
            comp[c] = resource(b, t1, t2, t3) << 2
            if changeable(t2):
                chain, t = [t2], t2
                while len(chain) < 8:
                    n = tdesc[t][5] if tdesc[t][5] < ntiles else 0
                    if n in chain:
                        break
                    chain.append(n)
                    if not changeable(n):
                        break
                    t = n
                if 0 not in chain:
                    chain.append(0)    # a bouncing brick: its cell shows what is under it
                for t in sorted(set(chain)):
                    variants.append((c, t, resource(b, t1, t, t3) << 2))
    variants.sort()
    if len(res_first) > MAX_RES:
        fail("a level has %d tile pictures, more than game/level.c's %d" % (len(res_first), MAX_RES))
    if len(variants) > 65535 or cells > 65535:
        fail("a level is too big for the 16-bit cell numbers")

    head = b"STX3" + struct.pack(">HHHHHhhHBBHHHHHHHI", W, H, track, ltime, grav, sx, sy, bgi, part, bscroll,
                                 nobj, autos, bspeed, len(res_first) - 1, len(anims), len(variants), len(blocks),
                                 len(frame_list) << 16)
    assert len(head) == 40
    body = bytearray(head)
    body += comp.astype(">u2").tobytes()
    for L in lay:
        body += L.astype(">u2").tobytes()
    body += struct.pack(">%dH" % len(res_first), *res_first)
    for a in anims:
        body += struct.pack(">HHHH", *a)
    body += struct.pack(">%dH" % len(frame_list), *frame_list)
    for v in variants:
        body += struct.pack(">HHH", *v)
    body += objs + resets
    return bytes(body), bytes(colours) + b"".join(blocks), dict(res=len(res_first) - 1, blocks=len(blocks), anims=len(anims), var=len(variants),
                      multi=multi_speed)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    data, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    rd = lambda n: open(os.path.join(data, n), "rb").read()
    tiles = rd("TILES.BIN")
    if tiles[:4] != b"STXT":
        fail("TILES.BIN is not stx_convert.py's")
    nframes, ntiles = struct.unpack(">HH", tiles[4:8])
    tdesc = [struct.unpack(">HHHHHH", tiles[8 + 12 * i:20 + 12 * i]) for i in range(ntiles)]
    frames = np.frombuffer(tiles[8 + 12 * ntiles:8 + 12 * ntiles + nframes * TILE * TILE], np.uint8).reshape(-1, TILE, TILE)

    backs = rd("BACKS.BIN")
    nb = struct.unpack(">H", backs[4:6])[0]
    back = []
    for i in range(nb):
        e = backs[6 + 24 * i:30 + 24 * i]
        w, h, off = struct.unpack(">HHI", e[16:24])
        back.append((backs[off:off + w * h], backs[off + w * h:off + w * h + 128], w, h))

    nlevels = len([l for l in open(os.path.join(data, "LEVELS.TXT")) if l.strip()])
    packs, stats = [], []
    gen_extra = 0
    for li in range(nlevels):
        lvb = rd("L%03d.BIN" % li)
        lb, cb, s = build_level(lvb, tdesc, frames, back)
        files = [("LEVEL.BIN", lb), ("COMP.BIN", cb)]
        if struct.unpack(">H", lvb[8:10])[0] != 0xFFFF:
            # REV3: the same level for OPTIONS - BACKGROUND GENESIS (the picture on the Genesis)
            lg, cg, _ = build_level(lvb, tdesc, frames, back, genesis=True)
            files += [("LEVELG.BIN", lg), ("COMPG.BIN", cg)]
            gen_extra += len(lg) + len(cg)
        packs.append(make_pack(files))
        stats.append(s)
    total = sum(len(p) for p in packs)
    print("  levels: %d, %d bytes (merged pictures: %d blocks, %d animated, %d cells with variants; "
          "%d stacks with two animation speeds)" % (nlevels, total, sum(s["blocks"] for s in stats),
                                                  sum(s["anims"] for s in stats), sum(s["var"] for s in stats),
                                                  sum(s["multi"] for s in stats)))
    print("  REV3: the levels' Genesis-background versions (LEVELG.BIN + COMPG.BIN): %d bytes" % gen_extra)

    # ---- homes: the demo in IMAGE8; area A from the end of IMAGE8 (world 1 from its first
    # level, as far as it goes); area B a ring for the rest, in play order
    res_files = ["PALETTE.BIN", "TILES.BIN", "SPRITES.BIN", "FONTS.BIN", "PICS.BIN", "WMTILES.BIN", "WORLDS.BIN",
                 "NAMES.TXT", "INTRO.TXT", "EXTRO.TXT", "EXTRO1.TXT", "EXTRO2.TXT", "CREDITS.TXT"]
    # R4: the background pictures for OPTIONS - BACKGROUND MOVES (SuperTux's bkgd_speed, game/
    # game.c): 320 x 240 each, rows of 336 (each followed by its own first 16 pixels again). They are
    # read only when the option is on and a level needs one - into the area B ring, like the levels (OJ1nn.PAK, nn = picture).
    nback = len(back)
    back_packs = []
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import stx_genesis
    for px, cols, bw, bh in back:
        rows = b"".join(px[y * bw:(y + 1) * bw] + px[y * bw:y * bw + TILE] for y in range(bh))
        # REV3: GENBG.BIN - the same picture for the Genesis' plane B (tools/stx_genesis.py)
        genbg = stx_genesis.genesis_background(px, cols, bw, bh)
        back_packs.append(make_pack([("BACK.BIN", struct.pack(">HHHH", bw, bh, bw + TILE, 0) + rows),
                                     ("GENBG.BIN", genbg)]))
    demo_bgi = 0xFFFF                  # (none resident: area A keeps room for world 1's levels)
    resident = [(n, rd(n)) for n in res_files] + [("DEMO.PAK", packs[DEMO_LEVEL])]
    homes = [None] * (nlevels + nback)

    # a level's background picture: picture index + 1 in the home's second byte (0: none - a
    # gradient), REV3: + 0x80 when it moves at its own speed (BACKGROUND MOVES reads it only then;
    # BACKGROUND GENESIS always - its GENBG.BIN is in the picture's pack)
    def back_of(li):
        lv = rd("L%03d.BIN" % li)
        bgi, bspeed = struct.unpack(">H", lv[8:10])[0], struct.unpack(">H", lv[30:32])[0]
        return 0 if bgi == 0xFFFF else (bgi + 1) | (0x80 if bspeed != 100 else 0)
    level_backs = [back_of(li) for li in range(nlevels)]

    def homes_bin():
        b = bytearray()
        for i, h in enumerate(homes):
            area, first, n, fid, flen = h if h else (0, 0, 0, 0, 0)
            b += struct.pack(">BBHHHI", area, level_backs[i] if i < nlevels else 0, first, n, fid, flen)
        return bytes(b)
    image8 = make_pack(resident + [("HOMES.BIN", homes_bin())])   # the size (HOMES.BIN's is fixed)
    a_first = (IMAGE8_AT + len(image8) + CHUNK - 1) // CHUNK * CHUNK
    if a_first > AREA_A_END:
        fail("IMAGE8.RAW is %d bytes: it runs into area B" % len(image8))
    pieces = lambda n: (n + CHUNK - 1) // CHUNK
    homes[DEMO_LEVEL] = (0, 0, 0, 0, 0)
    a_at = a_first
    image9 = bytearray()
    order = [i for i in range(nlevels) if i != DEMO_LEVEL]
    in_a = []
    blobs = packs + back_packs         # entries nlevels + i: background i
    a_order = order
    if demo_bgi != 0xFFFF:
        homes[nlevels + demo_bgi] = (0, 0, 0, 0, 0)
    for li in a_order:                 # area A: world 1 from level 1 while it fits
        n = pieces(len(blobs[li]))
        if a_at + n * CHUNK > AREA_A_END:
            break
        in_a.append(li)
        first = (a_at - AREA_A_SLOT * SLOT) // CHUNK
        image9 += b"\0" * (a_at - AREA_A_SLOT * SLOT - len(image9))
        image9 += blobs[li]
        homes[li] = [1, first, n, AREA_A_SLOT, 0]
        a_at += n * CHUNK
    for li in in_a:
        homes[li][4] = len(image9)
        homes[li] = tuple(homes[li])
    b_at = 0                           # area B: a ring
    b_size = AREA_B_END - AREA_B_AT
    manifest = ["IMAGE8.RAW"] + (["IMAGE9.RAW"] if in_a else [])
    for f in os.listdir(out):
        if f.upper().startswith("OJ") and f.upper().endswith(".PAK") or f.upper() == "IMAGE9.RAW":
            os.remove(os.path.join(out, f))
    b_order = [li for li in order if li not in in_a] + [nlevels + i for i in range(nback) if i != demo_bgi]
    for li in b_order:
        n = pieces(len(blobs[li]))
        if n * CHUNK > b_size:
            fail("level %d is %d bytes, more than area B" % (li, len(blobs[li])))
        if b_at + n * CHUNK > b_size:
            b_at = 0
        k = li + 1 if li < nlevels else 100 + (li - nlevels)     # OJ001-OJ077 levels, OJ100- backgrounds
        name = "OJ%03d.PAK" % k
        blob = b"\0" * b_at + blobs[li]
        open(os.path.join(out, name), "wb").write(blob)
        manifest.append(name)
        homes[li] = (2, b_at // CHUNK, n, 200 + k, len(blob))
        b_at += n * CHUNK
    image8 = make_pack(resident + [("HOMES.BIN", homes_bin())])
    if (IMAGE8_AT + len(image8) + CHUNK - 1) // CHUNK * CHUNK != a_first:
        fail("IMAGE8.RAW changed size")
    open(os.path.join(out, "IMAGE8.RAW"), "wb").write(image8)
    if in_a:
        open(os.path.join(out, "IMAGE9.RAW"), "wb").write(bytes(image9))
    open(os.path.join(out, "PACKS.TXT"), "w").write("\n".join(manifest) + "\n")
    print("  IMAGE8.RAW: %d bytes (%s, DEMO.PAK = the title's level %d bytes, HOMES.BIN)"
          % (len(image8), ", ".join(res_files), len(packs[DEMO_LEVEL])))
    print("  area A (IMAGE9.RAW, kept once read): 0x%06X-0x%06X, levels %s"
          % (a_first, a_at, ", ".join(str(i + 1) for i in in_a)))
    print("  area B (OJnnn.PAK, a ring of 0x%06X bytes): %d levels, %d background pictures (OJ1nn.PAK)"
          % (b_size, len([i for i in order if i not in in_a]), nback - (1 if demo_bgi != 0xFFFF else 0)))
    if any(h is None for h in homes):
        fail("not every level / background has a home")


if __name__ == "__main__":
    main()
