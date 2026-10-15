# R4 - no full redraw at 64 pixels, and the moving background as an option

(R3: docs/R3_NOTES.md. Micronut99 chose "parallax and 64-px", the background as
"Menu option, off".) Not yet run on the console.

## 1. The re-anchor moves pixels (game/yt_layers.c `reanchor`)

Vic's canvas is 384 px wide, 200 lines; the view (320 x 176) starts somewhere inside it. When
the camera goes 64 px sideways or 16 lines up/down past the canvas's anchor, R3 invalidated the
dirty matrix and redrew every cell (N 76-103, the 38-42 fps frames).

R4 keeps each buffer's anchor in words 1 and 2 of the dirty matrix area (extrafb 0x14C00:
word 0 is Vic's tm id, word 1 `top_h`, word 2 `top_v`). `yt_begin` compares the new anchor with
the buffer's own; when it moved by one or two steps:

- the canvas lines are moved by `dc` cells / `dr` cell rows (`yt_move_row`, 32-bit copies,
  safe in both directions; with `dr == 0` both SH2s share the lines through `plat_parallel`);
- the dirty matrix is shifted the same way and the cells coming in are set to -1, so only they
  are drawn.

A bigger jump (a reset point, a new level) still invalidates everything. On the PC the result
was compared pixel by pixel with R3 on 6 levels x 1500 frames: identical inside the view except
for item 3 below.

## 2. Options - Background <FIXED> / <MOVES>

SuperTux scrolls a level's background picture at `bkgd_speed` per cent of the camera (often 50).
R3 merges it into each cell's picture, so it moves with the tiles. With MOVES:

- `stx_pack.py` writes each level picture used by a `bkgd_speed != 100` level as `BACK.BIN`
  (w, h, stride = w + 16, then rows each followed by their first 16 pixels so a row can be
  copied without wrapping) into its own pack, `OJ100.PAK`-`OJ106.PAK` (file ids 300+n), with a
  home in area B like a level. HOMES.BIN gives each level the background's number + 1 ("back").
- `level.c` reads the background too when the option is on (`back_home`: not when the option is
  off, the level has none, or its home overlaps the level's own) - so a level may need a second
  read the first time.
- `game_draw_world` (game/game.c, the `drew_parallax` branch): `yt_back` copies the picture
  (176 rows, wrapping, 1-pixel shift parity), then the far particles, layers 1 and 2 as keyed
  sprites (`tiles_over_back`, skipping hidden cells), objects, Tux, layer 3, near particles, the
  scores. Everything is drawn every frame, so it is slower; it is off by default.
- Saved in bit 1 of options byte b[5] (bit 0 is the timing line). PC: `ST_PARALLAX=1` (any
  value, even empty, turns it on).

## 3. Small sprites at the edges

Vic's sprite loop needs at least 8 x 2 pixels; R3 dropped sprites clipped smaller than that (a
flicker at the screen edges). `yt_sprite` now draws them byte by byte with the key colour.

## 4. Fixed on the way

PALETTE.BIN was written from colour 1: every colour was shifted by one (coins looked silver).
It now starts with colour 0.

## Checked here

- All 77 levels, 3000 frames of random input, under the address / undefined-behaviour checkers,
  with the background FIXED and with MOVES: no errors, no missing variants.
- SH2 build: 44 KB copied to SDRAM, heap 42.6 KB; `check_sdram_code.py` passes;
  `check_boot.py`: release, 70 files identical.

## What to look at on the console

- Timing line while running right on a flat stretch: the old 38-42 dips every 64 px should be
  gone or smaller (N should stay low on those frames).
- Options - Background MOVES, then a level of Icyisland: the picture moving at half speed,
  tiles over it, F on the timing line. The first read of each level reads its background too.
- Coins gold, not silver.
