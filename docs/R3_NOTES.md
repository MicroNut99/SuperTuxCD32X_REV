# R3 - more frames, and the cart as a store of levels

Micronut99's R2 video (timing line on, Via Nostalgica): **F 17-22**, L 6.5-9.5, **T 22-29**,
S 1-2, **H 7-9**, W 0.5-6, **N 140-245** tiles a frame. R3 goes after T, N, H and L, and keeps
levels in the RAM cart so that they are read from the disc once.

**On the console (Micronut99's video, Welcome to Antarctica): F 38-56, L 1.9-2.8, T 3-9,
S 0.8-1.5, H 1-4, W 6-9, N 9-103** (R2: F 17-22). No crashes; the title starts with no read.

## 1. One merged picture a cell (T, N)

R2 gave Vic's tiler four layers (background picture, background-tm, interactive-tm,
foreground-tm). His dirty matrix keeps ONE tile number a cell, so every cell with a tile over
the background was drawn every frame - most of the screen (N 140-245).

`tools/stx_pack.py` now merges each cell's four layers into one 16x16 picture (identical
pictures stored once) and the tiler draws one layer. A cell is drawn only when it scrolls in,
when its picture changes (coin taken, box emptied, brick broken, an animation frame), or when a
sprite was over it in the last frame. On the PC: N 20-60 on ordinary screens (from 140-245),
about 100-150 with snow.

What had to come with it:

- **Changes to the map**: a cell whose interactive tile can change (coins, bricks, bonus boxes)
  has its other pictures made in advance (the "variants" table: coin gone, box empty, brick
  broken, brick hidden while it bounces). `level_change` / `level_hide` swap the cell's
  number; the tiler redraws it.
- **Animated tiles** (coins, water, the goal, flags) are animated pictures: at most 6 frames,
  identical frames stored once; `level_animate` points their resource at the frame of the time.
- **The foreground over Tux**: the merged picture holds the foreground-tm too, so after the
  sprites the game draws the foreground tiles again over the cells a sprite covers
  (`overlay_cells` in `game/game.c`, from the sprites' rectangles that `yt_sprite` logs).
- **Clouds and the far snow** are behind the tiles in SuperTux: after them, the cells they
  cover get background-tm, interactive-tm and foreground-tm drawn again on top.

The cost is data: levels are 110-430 KB (R2: 80-280 KB), 16.8 MB in all.

## 2. Exact sprite marks (N)

Vic's `draw_dirtyrect` shifts y right by 3 and then by 4 again: it marks every cell from line
y / 128 down to the sprite's bottom - a sprite in the lower half of the view made whole columns
redraw in the next frame. `yt_sprite` (game/yt_layers.c) now calls Vic's `draw_clip` and pixel
loop (`draw_handle_drawspritecmd`) as his `draw_sprite` does, and marks exactly the cells under
the sprite. Vic's files are unchanged.

## 3. Both SH2s draw the tiles (T)

`yt_layer` runs Vic's `draw_handle_layercmd` on both CPUs at once through the platform layer's
`plat_parallel` (port/plat_mars.c J10, the same job the OpenJazz port used for its second
CPU). Vic's function shares the cells out itself: each CPU takes the next cell number from his
counter in COMM6 under his TAS lock (`get_next_tile`) - exactly how YATSSD's own
`draw_tile_layer` uses the slave. The slave purges its cache before each job; the number of
tiles it drew is read past the caches.

## 4. The panel (H 8 ms)

The 32X's 1-pixel shift (odd scroll x) shifts the panel's lines too, so R2 rebuilt the panel -
letters pixel by pixel, then byte pairs - for each buffer whenever the camera moved. Now the
text is drawn only when it changes, into both shifted versions at once, and a buffer gets its
version copied in 32-bit words.

## 5. The busy code in SDRAM (L)

The SH2 fetches code from the cart slowly, and its 4 KB cache is shared with Vic's loops. The
rules, the level, Tux, the bad guys and the drawing calls (`res.c level.c tux.c objects.c
game.c yt_layers.c`, 33 KB) are now copied to SDRAM at start-up like Vic's code (mars/Makefile
HOT_OBJS: built without the loop / jump / label alignments, `.text` renamed into `.data`;
`tools/check_sdram_code.py` checks them too). SDRAM in use: 198 KB of 238 KB. Also: the
level's name is no longer looked up in NAMES.TXT every frame, and 24 snowflakes instead of 32.

## 6. The cart as a store of levels

```
0x000000  the program
0x100000  IMAGE8.RAW (644 KB) - read at power-on under the splash: all the shared data,
          + the title's demo level (DEMO.PAK) + HOMES.BIN
0x1A0000  area A: world 1's levels 1-4 - kept for good once read
0x280000  area B: every other level has a fixed place ("home") in this 1.4 MB ring
0x3F0000  the saves
```

- **The title never waits**: its demo level is part of IMAGE8.RAW.
- **A level read once is usually still there**: `level.c` remembers what is in the cart.
  World 1's first four levels stay; in area B, reading a level drops only the levels whose homes
  it overlaps (about the last 5-8 played stay). Dying, restarting, aborting and coming back,
  the title, the world map: no disc reads.
- **Faster reads**: the platform layer's `plat_load_pack` read the splash picture from the disc
  twice (its loading screen) before every level - 144 KB, about as much as the level itself.
  The game now sends the boot path's commands 38 + 46 itself (`cart_load`, game/yt_mars.c, the
  way plat_load_pack sends them) and reads only the level's own 32 KB pieces, showing the
  level's name and a bar.

Why the homes are fixed: command 38 opens a disc file for a fixed cart slot (IMAGE9.RAW ->
0x120000, OJnnn.PAK -> 0x280000) and command 46 copies piece c of it to slot + c x 32 KB, so a
level lands where it lies in its file. IMAGE9.RAW holds area A's levels at their places;
each OJnnn.PAK has zeros in front of its level up to its home (the zeros are never read - the
drive seeks past them; the disc gets 57 MB of level files). `tools/stx_pack.py` explains the
layout; `build.sh` checks every file in `PACKS.TXT` is on the disc.

Not possible: reading levels while music plays (the drive cannot play CD audio and read data at
once), and putting more into IMAGE8.RAW without a longer power-on wait.

## What to look at on the console

- The timing line on a plain stretch and on a snowy level: F, T and N, and the frames where the
  view moves 64 pixels sideways (the canvas is re-anchored: one full redraw, now by both CPUs).
- A level read: the name, "LOADING ..." and the bar; time it against R2.
- Clouds and snow behind the level's tiles; Tux behind foreground tiles; coins animating; boxes
  and bricks changing.
- Back to the title: no read. A level played again: no read.
