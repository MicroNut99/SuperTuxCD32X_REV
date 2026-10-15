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
