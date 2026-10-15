#!/usr/bin/env python3
"""stx_genesis.py - SuperTux CD32X REV3: a background picture for the Genesis video chip.

REV's goal is Sonic CD's way of drawing (README.md): the background on a Genesis tile plane,
scrolled by the Genesis chip's own scroll registers at no drawing cost, the 32X drawing the level
on top. This turns one of stx_convert.py's background pictures (320 x 240, 256 colours of the 32X)
into what the Genesis shows on plane B:

  - the picture at 256 x 192 (0.8 x the 32X's half size), so that two copies side by side fill
    the 64-cell (512-pixel) plane exactly: the plane wraps round with the picture, and scrolling
    sideways never needs new map entries (a 320-pixel picture does not divide 512);
  - 32 x 24 cells of 8 x 8 pixels, 4 bits a pixel, each cell using one of the 4 palettes of 15
    colours (colour 0 of a palette is see-through on the Genesis, so it is never used);
    the Genesis has 3 bits a colour channel (512 colours);
  - identical cells kept once (also mirrored ones: the map's flip bits).

GENBG.BIN (big-endian):
     0  "STGB"
     4  tiles u16 (N), pad u16
     8  64 x u16 CRAM words (4 palettes x 16: 0000BBB0GGG0RRR0)
   136  64 x 32 x u16 plane map (row-major, plane B at VRAM 0xE000): palette << 13 | vflip << 12 |
        hflip << 11 | tile (TILE_BASE + n)
  4232  N x 32 bytes: the tiles (8 rows of 4 bytes, the left pixel in the high nibble), for VRAM
        0x1000 (tile TILE_BASE)

VRAM (the 68000 listener's md_init_hw, cd32x/subcpu/hw_md.s InitVDPRegs): font 0x0000-0x0D5F,
our tiles from 0x1000 (tile 128), sprites 0xA800, H scroll 0xAC00, window 0xB000 (never written,
zero - game/level.c points plane A there while the background shows: the green / white text on
plane A at 0xC000 is hidden without clearing it), plane A 0xC000, plane B 0xE000, 64 x 32 cells.
"""
import struct

import numpy as np

TILE_BASE = 128                       # VRAM 0x1000
MAX_TILES = (0xA800 - 0x1000) // 32   # up to the sprite table
OUT_W, OUT_H = 256, 192
PLANE_W, PLANE_H = 64, 32


def _resize(rgb, w, h):
    """area-average resize (the picture only shrinks: 320 x 240 -> 256 x 192)"""
    from PIL import Image
    return np.asarray(Image.fromarray(rgb.astype(np.uint8), "RGB").resize((w, h), Image.BOX), np.float64)


def _kmeans(pts, k, iters=12, seed=1):
    """plain k-means (no sklearn on the build machine); pts n x d"""
    pts = np.asarray(pts, np.float64)
    n = len(pts)
    if n <= k:
        return pts.copy(), np.arange(n)
    rng = np.random.default_rng(seed)
    # k-means++ start
    c = [pts[rng.integers(n)]]
    for _ in range(1, k):
        d = np.min(((pts[:, None, :] - np.array(c)[None]) ** 2).sum(-1), axis=1)
        if d.sum() == 0:
            c.append(pts[rng.integers(n)])
        else:
            c.append(pts[rng.choice(n, p=d / d.sum())])
    c = np.array(c)
    lab = np.zeros(n, np.int64)
    for _ in range(iters):
        lab = np.argmin(((pts[:, None, :] - c[None]) ** 2).sum(-1), axis=1)
        for j in range(k):
            m = lab == j
            if m.any():
                c[j] = pts[m].mean(0)
    return c, lab


def _q3(v):
    """0..255 -> the Genesis' 8 levels per channel (0, 36, 73, ... 255 as shown)"""
    return np.clip(np.rint(np.asarray(v) / 255.0 * 7), 0, 7).astype(np.int64)


def genesis_background(px, pal555, w, h):
    """px: w*h bytes (indices into the 32X palette part pal555: 64 x u16 for entries 192-255),
    returns GENBG.BIN"""
    idx = np.frombuffer(px, np.uint8).reshape(h, w).astype(np.int64)
    cols = np.frombuffer(pal555, ">u2").astype(np.int64)
    lut = np.zeros((256, 3))
    for i in range(256):
        c = cols[i - 192] if 192 <= i < 256 and i - 192 < len(cols) else 0
        lut[i] = ((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3)
    rgb = _resize(lut[idx], OUT_W, OUT_H)
    tw, th = OUT_W // 8, OUT_H // 8
    tiles = rgb.reshape(th, 8, tw, 8, 3).transpose(0, 2, 1, 3, 4).reshape(th * tw, 64, 3)

    # 4 palettes: tiles grouped by their colours (mean + spread), 15 colours each
    feat = np.concatenate([tiles.mean(1), tiles.std(1)], axis=1)
    _, group = _kmeans(feat, 4)
    cram = np.zeros((4, 16, 3), np.int64)
    for g in range(4):
        m = group == g
        if not m.any():
            continue
        # the colours the Genesis can show closest to these pixels (3 bits a channel), then the
        # 15 of them that cover the pixels best (k-means on the grid colours, snapped back to the
        # grid each round: rounding free centres afterwards shifted the hues)
        grid = _q3(tiles[m].reshape(-1, 3))
        uniq, cnt = np.unique(grid, axis=0, return_counts=True)
        if len(uniq) <= 15:
            q = uniq
        else:
            order = np.argsort(-cnt)
            c = uniq[order[:15]].astype(np.float64)
            for _ in range(12):
                lab = np.argmin(((uniq[:, None, :] - c[None]) ** 2).sum(-1), axis=1)
                for j in range(15):
                    mm = lab == j
                    if mm.any():
                        c[j] = (uniq[mm] * cnt[mm, None]).sum(0) / cnt[mm].sum()
                c = np.clip(np.rint(c), 0, 7)
            q = np.unique(c.astype(np.int64), axis=0)
        cram[g, 1:1 + len(q)] = q
        cram[g, 1 + len(q):] = q[-1]
    cram[:, 0] = 0                    # colour 0: see-through, never used by a pixel; palette 0's is
                                      # the backdrop - black, as before the picture was there
    shown = cram * 255.0 / 7.0        # what each palette entry looks like

    # each tile: the palette that fits it best, then its pixels to colours 1-15 of that palette,
    # with a 4 x 4 ordered dither (the 3-bit steps of a sky gradient become a fine pattern)
    bayer = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0 - 0.5
    dith = np.tile(bayer, (2, 2)).reshape(64, 1) * (255.0 / 7.0) * 0.75
    tile_px = np.zeros((len(tiles), 64), np.int64)
    tile_pal = np.zeros(len(tiles), np.int64)
    for t in range(len(tiles)):
        best = None
        for g in range(4):
            d = ((tiles[t][:, None, :] - shown[g, 1:][None]) ** 2).sum(-1)
            e = d.min(1).sum()
            if best is None or e < best[0]:
                best = (e, g)
        g = best[1]
        d = (((tiles[t] + dith)[:, None, :] - shown[g, 1:][None]) ** 2).sum(-1)
        tile_pal[t], tile_px[t] = g, d.argmin(1) + 1

    # identical tiles once (also mirrored)
    store, store_of, cell = [], {}, []
    for t in range(len(tiles)):
        a = tile_px[t].reshape(8, 8)
        hit = None
        for hf in (0, 1):
            for vf in (0, 1):
                b = a[:, ::-1] if hf else a
                b = b[::-1] if vf else b
                key = b.astype(np.uint8).tobytes()
                if key in store_of:
                    hit = (store_of[key], hf, vf)
                    break
            if hit:
                break
        if not hit:
            store_of[a.astype(np.uint8).tobytes()] = len(store)
            store.append(a)
            hit = (len(store) - 1, 0, 0)
        cell.append((tile_pal[t] << 13) | (hit[2] << 12) | (hit[1] << 11) | (TILE_BASE + hit[0]))
    if len(store) > MAX_TILES:
        raise SystemExit("*** a Genesis background needs %d tiles, more than the %d below the sprite table"
                         % (len(store), MAX_TILES))

    plane = np.zeros((PLANE_H, PLANE_W), np.int64)
    cellmap = np.array(cell).reshape(th, tw)
    for y in range(PLANE_H):
        for x in range(PLANE_W):
            plane[y, x] = cellmap[y % th, x % tw]    # two copies side by side; rows wrap round
    cramw = [(int(b) << 9) | (int(g) << 5) | (int(r) << 1) for r, g, b in cram.reshape(64, 3)]
    out = bytearray(b"STGB" + struct.pack(">HH", len(store), 0))
    out += struct.pack(">64H", *cramw)
    out += plane.astype(">u2").tobytes()
    for a in store:
        for row in a:
            out += bytes(int(row[2 * i]) << 4 | int(row[2 * i + 1]) for i in range(4))
    return bytes(out)


def preview(genbg, path):
    """the plane as the Genesis shows it (the first 256 x 192), as PNG - for build/previews"""
    from PIL import Image
    n = struct.unpack(">H", genbg[4:6])[0]
    cram = struct.unpack(">64H", genbg[8:136])
    plane = np.frombuffer(genbg[136:136 + 4096], ">u2").reshape(PLANE_H, PLANE_W)
    tiles = np.frombuffer(genbg[4232:4232 + 32 * n], np.uint8).reshape(n, 8, 4)
    img = np.zeros((OUT_H, OUT_W, 3), np.uint8)
    for cy in range(OUT_H // 8):
        for cx in range(OUT_W // 8):
            e = int(plane[cy, cx])
            t = tiles[(e & 0x7FF) - TILE_BASE]
            px = np.stack([t >> 4, t & 15], -1).reshape(8, 8)
            if e & 0x800:
                px = px[:, ::-1]
            if e & 0x1000:
                px = px[::-1]
            pal = (e >> 13) & 3
            for y in range(8):
                for x in range(8):
                    c = cram[pal * 16 + px[y, x]]
                    img[cy * 8 + y, cx * 8 + x] = [((c >> 1) & 7) * 255 // 7, ((c >> 5) & 7) * 255 // 7, ((c >> 9) & 7) * 255 // 7]
    Image.fromarray(img).save(path)


# =================================================================================== REV4
# OPTIONS - DRAW: GENESIS - the whole level on the Genesis (Sonic CD's way): the level's tiles on
# plane A, the background on plane B, both scrolled by the Genesis chip; the 32X draws only the
# sprites. VRAM (game/gen_level.c uses the same numbers):
#   0x0000-0x0FFF  Chilly's font (the boot text's) - tiles 0-127, tile 0 = the blank space
#   0x1000-0x47FF  background tiles 128-575 (BG4_MAX = 448): GENBG1.BIN / a level's gradient
#   0x4800-0x9FFF  level tiles 576-1279      \  LEVEL_SLOTS: 832 tiles for a level's static
#   0xA000-0xAFFF  plane B map (64 x 32)     |  pictures and its animation slots
#   0xB000-0xBFFF  level tiles 1408-1535     /
#   0xC000-0xCFFF  plane A of the boot text (never touched, never shown in a level)
#   0xD000         sprite table (empty), 0xD400 H scroll table (plane A word, plane B word)
#   0xE000-0xEFFF  plane A map (64 x 32): the level
# Palettes: 0-2 the level (45 colours), 3 the background.
BG4_BASE, BG4_MAX = 128, 448
LEVEL_SLOTS = list(range(576, 1280)) + list(range(1408, 1536))


def _grid_palette(rgb_pts, n=15, weights=None):
    """up to n colours of the Genesis' 3-bit grid that cover these pixels best (grid units 0-7)"""
    grid = _q3(rgb_pts)
    uniq, inv = np.unique(grid, axis=0, return_inverse=True)
    cnt = np.bincount(inv.ravel(), weights=weights, minlength=len(uniq)).astype(np.float64)
    if len(uniq) <= n:
        return uniq
    order = np.argsort(-cnt)
    c = uniq[order[:n]].astype(np.float64)
    for _ in range(12):
        lab = np.argmin(((uniq[:, None, :] - c[None]) ** 2).sum(-1), axis=1)
        for j in range(n):
            mm = lab == j
            if mm.any():
                c[j] = (uniq[mm] * cnt[mm, None]).sum(0) / cnt[mm].sum()
        c = np.clip(np.rint(c), 0, 7)
    return np.unique(c.astype(np.int64), axis=0)


def _cram_words(cram):
    return [(int(b) << 9) | (int(g) << 5) | (int(r) << 1) for r, g, b in cram]


def _dedupe(tile_list):
    """8x8 index tiles -> (stored tiles, per input (index, hflip, vflip)); mirrored ones once"""
    store, store_of, ref = [], {}, []
    for a in tile_list:
        hit = None
        for hf in (0, 1):
            for vf in (0, 1):
                b = a[:, ::-1] if hf else a
                b = b[::-1] if vf else b
                k = b.astype(np.uint8).tobytes()
                if k in store_of:
                    hit = (store_of[k], hf, vf)
                    break
            if hit:
                break
        if not hit:
            store_of[a.astype(np.uint8).tobytes()] = len(store)
            store.append(a.copy())
            hit = (len(store) - 1, 0, 0)
        ref.append(hit)
    return store, ref


def _reduce(store, ref, cap, shown_of):
    """more stored tiles than cap: the closest ones share one (k-means on what they look like -
    shown_of(i) = 64 x 3 colours of stored tile i); returns the new store and ref"""
    if len(store) <= cap:
        return store, ref
    look = np.array([shown_of(i).reshape(-1) for i in range(len(store))])
    cent, lab = _kmeans(look, cap, iters=8)
    keep = []
    rep_of = {}
    for j in range(cap):
        members = np.nonzero(lab == j)[0]
        if not len(members):
            continue
        best = members[np.argmin(((look[members] - cent[j]) ** 2).sum(-1))]
        rep_of[j] = len(keep)
        keep.append(store[best])
    new_ref = [(rep_of[lab[i]], hf, vf) for (i, hf, vf) in ref]
    return keep, new_ref


def _tile_bytes(a):
    return b"".join(bytes(int(row[2 * i]) << 4 | int(row[2 * i + 1]) for i in range(4)) for row in a)


def _plane_picture(rgb, slot, base, cap, dither=True):
    """an RGB picture (w x h, multiples of 8, dividing the 512 x 256 plane) on the Genesis with ONE
    palette (slot): (cram 16 words, plane map 64 x 32, tile bytes)"""
    h, w = rgb.shape[:2]
    tw, th = w // 8, h // 8
    tiles = rgb.reshape(th, 8, tw, 8, 3).transpose(0, 2, 1, 3, 4).reshape(th * tw, 64, 3)
    q = _grid_palette(tiles.reshape(-1, 3))
    cram = np.zeros((16, 3), np.int64)
    cram[1:1 + len(q)] = q
    cram[1 + len(q):] = q[-1]
    shown = cram * 255.0 / 7.0
    bayer = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0 - 0.5
    dith = np.tile(bayer, (2, 2)).reshape(64, 1) * (255.0 / 7.0) * (0.75 if dither else 0)
    idx = [((((t + dith)[:, None, :] - shown[1:][None]) ** 2).sum(-1).argmin(1) + 1).reshape(8, 8) for t in tiles]
    store, ref = _dedupe(idx)
    store, ref = _reduce(store, ref, cap, lambda i: shown[store[i].reshape(-1)])
    cell = [(slot << 13) | (vf << 12) | (hf << 11) | (base + i) for (i, hf, vf) in ref]
    cellmap = np.array(cell).reshape(th, tw)
    plane = np.array([[cellmap[y % th, x % tw] for x in range(PLANE_W)] for y in range(PLANE_H)])
    return _cram_words(cram), plane, b"".join(_tile_bytes(a) for a in store), len(store)


def picture_plane(px, pal555, w, h):
    """REV5: a background picture as plane B parts (palette 3 CRAM words, 64 x 32 map, tiles, n) -
    used by GENBG1.BIN and embedded in every picture level's GENLVL.BIN"""
    idx = np.frombuffer(px, np.uint8).reshape(h, w).astype(np.int64)
    cols = np.frombuffer(pal555, ">u2").astype(np.int64)
    lut = np.zeros((256, 3))
    for i in range(192, 256):
        c = cols[i - 192] if i - 192 < len(cols) else 0
        lut[i] = ((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3)
    rgb = _resize(lut[idx], OUT_W, OUT_H)
    return _plane_picture(rgb, 3, BG4_BASE, BG4_MAX)


def genesis_background1(px, pal555, w, h):
    """REV4: the background picture with ONE palette (slot 3) and at most BG4_MAX tiles - the other
    three palettes and the rest of VRAM belong to the level. GENBG1.BIN: like GENBG.BIN (64 CRAM
    words, only 48-63 used), the map's tiles from BG4_BASE"""
    idx = np.frombuffer(px, np.uint8).reshape(h, w).astype(np.int64)
    cols = np.frombuffer(pal555, ">u2").astype(np.int64)
    lut = np.zeros((256, 3))
    for i in range(192, 256):
        c = cols[i - 192] if i - 192 < len(cols) else 0
        lut[i] = ((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3)
    cram16, plane, tbytes, n = picture_plane(px, pal555, w, h)
    out = bytearray(b"STGB" + struct.pack(">HH", n, 1))
    out += struct.pack(">64H", *([0] * 48 + cram16))
    out += plane.astype(">u2").tobytes()
    out += tbytes
    return bytes(out)


def gradient_plane(top555, bot555, rows_px=240):
    """a level's gradient (SuperTux's drawgradient: top colour to bottom colour over the level's
    height) as plane B: 512 x 256, one palette (slot 3), dithered; tied to the level's lines (the
    game scrolls plane B up / down with plane A). Same layout as GENBG1.BIN's parts."""
    rgb_of = lambda c: np.array(((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3), np.float64)
    t, b = rgb_of(top555), rgb_of(bot555)
    img = np.zeros((PLANE_H * 8, PLANE_W * 8, 3))
    for y in range(PLANE_H * 8):
        s = min(63, y * 64 // rows_px)
        img[y, :] = t + (b - t) * s / 63
    return _plane_picture(img, 3, BG4_BASE, BG4_MAX)


def genesis_level(level_bin, comp_bin, global_pal, grad=None, picture=None):
    """REV4: GENLVL.BIN - a level's see-through pictures (stx_pack.py LEVELG.BIN / COMPG.BIN) as
    Genesis tiles for plane A (palettes 0-2), each resource's 2 x 2 map entries, the level's map
    as COLUMNS (identical columns once: game/gen_level.c streams one per 8 pixels scrolled), the
    animations as groups whose frames are uploaded over fixed tiles, and for a gradient level its
    plane B. Big-endian:
       0  "STGL", 4 version u16 (1), 6 level tiles u16 (T), 8 resources u16 (R + 1),
      10  map columns u16 (2 w), 12 rows u16 (2 h), 14 unique columns u16 (U), 16 groups u16 (G),
      18  plane B tiles u16, 20 plane B kind u16 (0 gradient, tied to the level's lines;
          1 picture, 256 x 192 moving at the level's background speed; REV5: always here;
          REV8: 2 none - an empty map, the 32X draws the sky)
      24  offsets u32: +0 CRAM (48 words: palettes 0-2, then 16 for palette 3 when plane B is
          here), +4 tiles (T x 32 bytes, for LEVEL_SLOTS[0..T)), +8 top entries ((R+1) x 2 words),
          +12 bottom entries ((R+1) x 2 words), +16 column index (2 w x u16), +20 columns
          (U x rows words), +24 groups (G x {ms u16, frames u16, first slot u16, tiles u16,
          frames' tiles u32}), +28 plane B map (64 x 32 words), +32 plane B tiles
      60  the data
    """
    W, H = struct.unpack(">HH", level_bin[4:8])
    nres, nanims, nvars, nblocks = struct.unpack(">HHHH", level_bin[28:36])
    nframes = struct.unpack(">H", level_bin[36:38])[0]
    cells = W * H
    p = 40
    comp = np.frombuffer(level_bin[p:p + 2 * cells], ">u2").astype(np.int64); p += 2 * cells
    p += 3 * 2 * cells
    res_first = np.frombuffer(level_bin[p:p + 2 * (nres + 1)], ">u2").astype(np.int64); p += 2 * (nres + 1)
    anims = [struct.unpack(">HHHH", level_bin[p + 8 * a:p + 8 * a + 8]) for a in range(nanims)]; p += 8 * nanims
    flist = np.frombuffer(level_bin[p:p + 2 * nframes], ">u2").astype(np.int64)
    blocks = np.frombuffer(comp_bin[128:], np.uint8).reshape(-1, 16, 16)
    gp = np.frombuffer(global_pal, ">u2").astype(np.int64)
    lut = np.zeros((256, 3))
    for i in range(1, min(192, len(gp))):
        c = gp[i]
        lut[i] = ((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3)
    transparent = lambda a: (a == 0) | (a >= 192)
    quads = lambda blk: [blk[y:y + 8, x:x + 8] for y in (0, 8) for x in (0, 8)]   # TL TR BL BR

    # the units that get a palette: a static resource's quadrant, or an animated resource's quadrant
    # over all its frames (its slot tile keeps one palette while its pixels change)
    anim_of = {a[0]: a for a in anims if a[2] and a[1]}
    units = []                         # (resource, quadrant, [8x8 index arrays])
    for r in range(1, nres + 1):
        if r in anim_of:
            r_, nf, ms, first = anim_of[r]
            fr = [blocks[flist[first + k]] for k in range(nf)]
            for q in range(4):
                units.append((r, q, [quads(b)[q] for b in fr]))
        else:
            for q in range(4):
                units.append((r, q, [quads(blocks[res_first[r]])[q]]))

    # 3 palettes (REV8: 4 - plane B is empty, the 32X draws the sky, so palette 3 is the level's
    # too): units grouped by their visible colours, 15 colours each
    NP = 4 if picture is None and grad is None else 3
    def vis(u):
        pts = np.concatenate([lut[a[~transparent(a)]] for a in u[2]]) if u[2] else np.zeros((0, 3))
        return pts
    feats, has = [], []
    for u in units:
        v = vis(u)
        has.append(len(v) > 0)
        feats.append(np.concatenate([v.mean(0), v.std(0)]) if len(v) else np.zeros(6))
    feats = np.array(feats)
    vis_idx = [i for i in range(len(units)) if has[i]]
    _, glab = _kmeans(feats[vis_idx], NP) if len(vis_idx) >= NP else (None, np.zeros(len(vis_idx), np.int64))
    cram = np.zeros((NP, 16, 3), np.int64)
    for g in range(NP):
        mem = [vis_idx[i] for i in range(len(vis_idx)) if glab[i] == g]
        if not mem:
            continue
        q = _grid_palette(np.concatenate([vis(units[i]) for i in mem]))
        cram[g, 1:1 + len(q)] = q
        cram[g, 1 + len(q):] = q[-1]
    shown = cram * 255.0 / 7.0

    def fit(u):
        """the palette that shows the unit best, and its pixels in it (0 = see-through)"""
        best = None
        for g in range(NP):
            e = 0.0
            for a in u[2]:
                m = ~transparent(a)
                if m.any():
                    e += (((lut[a[m]][:, None, :] - shown[g, 1:][None]) ** 2).sum(-1)).min(1).sum()
            if best is None or e < best[0]:
                best = (e, g)
        g = best[1]
        out = []
        for a in u[2]:
            o = np.zeros((8, 8), np.int64)
            m = ~transparent(a)
            if m.any():
                o[m] = (((lut[a[m]][:, None, :] - shown[g, 1:][None]) ** 2).sum(-1)).argmin(1) + 1
            out.append(o)
        return g, out

    # static tiles (deduped, also mirrored) and animation slots (grouped by speed and length)
    static_units = [(i, u) for i, u in enumerate(units) if u[0] not in anim_of]
    s_fit = {i: fit(u) for i, u in static_units}
    s_store, s_ref = _dedupe([s_fit[i][1][0] for i, u in static_units])
    groups = {}                        # (ms, frames) -> [resource, ...]
    for r in sorted(anim_of):
        _, nf, ms, _ = anim_of[r]
        groups.setdefault((ms, nf), []).append(r)
    n_slots = sum(4 * len(v) for v in groups.values())
    cap = len(LEVEL_SLOTS) - n_slots
    if cap < 64:
        raise SystemExit("*** a level has %d animation slot tiles, too many for VRAM" % n_slots)
    # (a stored tile may serve units in different palettes: it is judged in the first one's)
    pal_of_store = {}
    for k, (i, u) in enumerate(static_units):
        pal_of_store.setdefault(s_ref[k][0], s_fit[i][0])
    s_store, s_ref = _reduce(s_store, s_ref, cap, lambda i: shown[pal_of_store.get(i, 0)][s_store[i].reshape(-1)])
    entry = {}                         # (resource, quadrant) -> map entry
    for k, (i, u) in enumerate(static_units):
        si, hf, vf = s_ref[k]
        entry[(u[0], u[1])] = (s_fit[i][0] << 13) | (vf << 12) | (hf << 11) | LEVEL_SLOTS[si]
    slot = len(s_store)
    grp_out, slab = [], bytearray()
    unit_at = {(u[0], u[1]): i for i, u in enumerate(units)}
    for (ms, nf), rs in sorted(groups.items()):
        first = slot
        fits = {}
        for r in rs:
            for q in range(4):
                g, frames_px = fit(units[unit_at[(r, q)]])
                fits[(r, q)] = frames_px
                entry[(r, q)] = (g << 13) | LEVEL_SLOTS[slot]
                slot += 1
        if LEVEL_SLOTS[first] + 4 * len(rs) - 1 != LEVEL_SLOTS[slot - 1]:
            raise SystemExit("*** an animation group crosses the VRAM gap")   # (kept contiguous)
        off = len(slab)
        for k in range(nf):
            for r in rs:
                for q in range(4):
                    slab += _tile_bytes(fits[(r, q)][k])
        grp_out.append((ms, nf, LEVEL_SLOTS[first], 4 * len(rs), off))

    top_e, bot_e = [0, 0], [0, 0]      # resource 0: nothing (tile 0, the blank font space)
    for r in range(1, nres + 1):
        top_e += [entry[(r, 0)], entry[(r, 1)]]
        bot_e += [entry[(r, 2)], entry[(r, 3)]]

    # the map as columns (Genesis cell columns: 2 per level cell), identical ones once
    rows = 2 * H
    col_index, col_store, col_of = [], [], {}
    res_at = (comp >> 2).reshape(H, W)
    for x in range(2 * W):
        col = []
        for y in range(rows):
            r = int(res_at[y // 2, x // 2])
            col.append((top_e if y % 2 == 0 else bot_e)[2 * r + (x & 1)])
        key = tuple(col)
        if key not in col_of:
            col_of[key] = len(col_store)
            col_store.append(col)
        col_index.append(col_of[key])

    # plane B for a gradient level
    gb_cram, gb_map, gb_tiles, gb_n = ([], None, b"", 0)
    kind = 0
    if picture is not None:
        gb_cram, gb_map, gb_tiles, gb_n = picture
        kind = 1
    elif grad is not None:
        gb_cram, gb_map, gb_tiles, gb_n = gradient_plane(*grad)
    else:
        # REV8: no Genesis sky (the 32X draws it): plane B an empty map - tile 0 is the font's
        # blank tile, all see-through - and palette 3 is the level's fourth
        gb_cram, gb_map, gb_tiles, gb_n = [], np.zeros((PLANE_H, PLANE_W), np.int64), b"", 0
        kind = 2

    head_len = 60
    parts = []
    cram_w = _cram_words(cram.reshape(NP * 16, 3)) + list(gb_cram)   # 64 words either way
    parts.append(struct.pack(">%dH" % len(cram_w), *cram_w))
    parts.append(b"".join(_tile_bytes(a) for a in s_store))
    parts.append(struct.pack(">%dH" % len(top_e), *top_e))
    parts.append(struct.pack(">%dH" % len(bot_e), *bot_e))
    parts.append(struct.pack(">%dH" % len(col_index), *col_index))
    parts.append(b"".join(struct.pack(">%dH" % rows, *c) for c in col_store))
    gb = bytearray()
    for (ms, nf, fs, nt, off) in grp_out:
        gb += struct.pack(">HHHHI", ms, nf, fs, nt, 0)
    parts.append(bytes(gb))
    parts.append(gb_map.astype(">u2").tobytes() if gb_map is not None else b"")
    parts.append(gb_tiles)
    parts.append(bytes(slab))
    offs, at = [], head_len
    for prt in parts:
        at = (at + 3) & ~3
        offs.append(at)
        at += len(prt)
    # the groups' frame tiles: after everything (offsets inside the slab -> file offsets)
    slab_at = offs[-1]
    gb = bytearray()
    for (ms, nf, fs, nt, off) in grp_out:
        gb += struct.pack(">HHHHI", ms, nf, fs, nt, slab_at + off)
    parts[6] = bytes(gb)
    head = b"STGL" + struct.pack(">HHHHHHHHH", 1, len(s_store), nres + 1, 2 * W, rows, len(col_store),
                                 len(grp_out), gb_n, kind)
    head += b"\0" * (24 - len(head))
    head += struct.pack(">9I", *offs[:9])
    out = bytearray(head)
    for prt, o in zip(parts, offs):
        out += b"\0" * (o - len(out))
        out += prt
    assert len(head) == 60
    return bytes(out), dict(tiles=len(s_store), slots=n_slots, cols=len(col_store), groups=len(grp_out))
