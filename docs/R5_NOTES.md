# R5 - plain backgrounds, fewer cells drawn a frame

(R4: docs/R4_NOTES.md.) Micronut99: "let go back to the original backgrounds. I dont like the
sharp" and "How can we improve the frame rate even more?". Not yet run on the console.

## Where the time went

R3 on the console (Welcome to Antarctica): F 38-56, L 1.9-2.8, T 3-9, S ~1, H 1-4, W 6-9 ms.
The work of a frame is mostly drawing. On the PC (callgrind, Bonus Island I's busiest level),
the game's rules are about 4 % of the instructions and drawing pixels about 75 %. A frame
misses the 60 Hz refresh when its drawing runs long: snow, clouds, many sprites, water. R5 cuts
the cells the tiler has to draw (N) and the sprite pixels. It does not touch Vic's files, the
platform layer (`port/`) or the boot path.

Measured on the PC (`ST_STATS=1`: Tux running right and jumping, 1500 frames at 60 Hz,
`host/stx_host`). N is cells drawn per frame, mean (90 % of frames below):

| Level | R4 N | R5 N | R4 sprite pixels | R5 sprite pixels |
|---|---|---|---|---|
| 1 Welcome to Antarctica | 14.0 (21) | 13.4 (21) | 813 | 807 |
| 7 Oh no! More Snowballs! (snow) | 40.9 (52) | 24.1 (34) | 6756 | 3565 |
| 9 Grumbel's Sense of Snow (snow) | 50.0 (65) | 30.0 (40) | 6424 | 2093 |
| 11 Night Chill (snow) | 42.1 (53) | 25.3 (37) | 10349 | 7178 |
| 12 Into the Stars (snow) | 44.7 (57) | 28.3 (39) | 8504 | 5518 |
| 13 Above the Arctic Skies (clouds) | 79.8 (101) | 47.6 (73) | 15138 | 10406 |

On the PC the picture is the same as R4's except where noted below (R4's drawing and R5's were
run on the same data and every third frame compared pixel by pixel).

## 1. The backgrounds are not sharpened

`tools/stx_convert.py SHARPEN_BACKGROUNDS = False`: the level backgrounds and the menu pages'
pictures (title, extro, credits) are halved with Lanczos only, as SuperTux's own pictures look.
Tiles, sprites and the logo stay sharpened (R2's choice). True brings R2-R4's look back.

## 2. Sprites cut to their visible pixels

Every cell a sprite touches is drawn again by the tiler two frames later (in that buffer), so a
sprite's see-through margins cost real time. SuperTux's snowflakes are 32 x 32 pictures with
a 17 x 15, 8 x 7 or 11 x 9 flake in them: at half size a flake covered up to 4 cells, now
mostly 1-2. `stx_convert.py trimmed()` cuts every sprite frame (not the logo) to its visible
pixels; SPRITES.BIN ("STXT", was "STXS") keeps where the cut picture sits (dx, dy, dx even so
the pixels land where they did). `spr_frame_at()` in game/res.c returns it; `spr_draw`,
`spr_draw_part` and the particles add it. Pixel for pixel the same picture.

## 3. The cells are listed, then drawn by both SH2s (game/yt_layers.c yt_layer)

R3/R4 ran Vic's `draw_handle_layercmd` on both CPUs: for every cell of the view (about 260
a frame, drawn or not) each CPU took the next cell number from COMM6 under his TAS lock - two
locked, uncached accesses and a register round trip per cell. Now the master walks the view
once (the same rule: draw a cell when the dirty matrix does not hold its tile), lists the
cells and updates the dirty matrix; each CPU then draws every second cell of the list with
Vic's pixel loop (`draw_handle_drawspritecmd`, what his `draw_drawtile` does). With 4 cells or
fewer the second SH2 is not started. Pixel for pixel the same. `-DYT_VIC_TILER` goes back.

## 4. Kept clouds (game/game.c clouds_before / clouds_draw)

A cloud is 64 x 32, behind the tiles, fixed in the world sideways, drifting 12-22 pixels a
second. R3/R4 drew the clouds every frame as sprites, then drew the level's tiles over them
again, and the tiler drew their 10-15 cells again two frames later. Now each buffer keeps its
clouds in its canvas:

- drawn into the whole canvas (the view and the columns / lines around it, `yt_keep`), so a
  cloud at the edge of the view needs nothing when the view scrolls;
- a cloud that moved (its 2-pixel steps, or up / down with the camera) gets its old cells
  marked (the tiler draws them clean) and is drawn whole;
- a cloud that did not move is drawn again only in the cells the tiler drew under it this
  frame (`yt_watch` / `yt_watch_changed`: Tux went past, a cloud in front moved away), cut to
  their box (`yt_keep_box`);
- clouds are drawn in their order; cells a cloud was drawn over make the clouds in front of it
  draw there too;
- then the level's tiles are drawn again over every cell that got cloud pixels.

Each buffer remembers where it drew each cloud (`cseen[2][]`, world pixels). A cloud's canvas
column is kept even so a part drawn in a box lands on the same pixels as the whole cloud.
Differences from R4 on the PC: a cloud cut by the left edge of the view was drawn 1 pixel to
the left by Vic's clip (his rounding of a clipped sprite); a kept cloud does not jump. A cloud
at the very edge of the view now has the tiles drawn over it where R4 missed them.

## 5. Far snow only where no tile is

A far snowflake (behind the tiles) is drawn only where the cells under it hold no tile in
layers 1-3. Over a tile it is hidden anyway; R3/R4 paid for that by drawing the tiles of its
cells over it again and the tiler those cells again next frame. A flake can now disappear up to
a cell early at the edge of a tile with see-through parts.

## 6. Animated tiles: only the ones that changed (game/level.c level_mark_animated)

R3/R4 marked every animated cell in view whenever any animation changed its frame (every
25 ms step something does), so a level with a waterfall and coins drew all of them all the
time. Now each buffer remembers the frame of each animation it last drew (`anim_seen[2][]`) and
marks only the cells of the animations that changed. The marks cover the whole canvas, so a
cell outside the view does not come in with an old frame.

## PC-only tools (game/host.c)

- `ST_STATS=1`: a line per frame on stderr, `STAT frame cells sprites sprite-pixels cx cy`.
- `ST_NOPARTICLES=1`: no snow or clouds (what they cost).
- `STX_NOTRIM=1 tools/stx_convert.py ...`: sprites not cut (to compare).

## Checked here

- All 77 levels, 3000 frames of random input, under the address / undefined-behaviour
  checkers, with BACKGROUND FIXED and MOVES: no errors.
- Pixel comparisons against R4's drawing: Welcome to Antarctica, Via Nostalgica, level 7,
  level 13 (clouds), Bonus Island II level 10 (clouds, waterfall), the title and the world map.
  The differences are listed above (clouds at the edges, far flakes near tiles).
- SH2 build: 44.7 KB copied to SDRAM, heap 35.2 KB (R4: 42.6 KB; nothing in the game mallocs on
  the 32X); `check_sdram_code.py` passes; `check_boot.py`: release, 70 files identical.

## What to look at on the console

- The timing line on Above the Arctic Skies (clouds) and Night Chill (snow): F and N against
  R4; the clouds drifting and Tux passing in front of them (no left-over cloud pieces).
- Welcome to Antarctica: F should stay at R4's level or better (T a little lower: no locks).
- The backgrounds: soft, as SuperTux's.
- Coins and water animating; a coin taken next to a waterfall.

## Not done (and why)

- The platform layer paces frames to the median of the last 16 (`port/plat_mars.c`
  `pace_before_flip`, OJ32X_PACE): with frame times around 16-17 ms it holds fast frames back
  to 2 refreshes. Switching it off could show more frames but with uneven motion; it is in
  `port/`, which stays as proven.
- The clouds' drift itself (a 2-pixel step every few frames) still costs a cloud's cells each
  step; 4-pixel steps would halve it but look steppy.
