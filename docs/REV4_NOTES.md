# REV4 – the rip and tear: the level on the Genesis

REV3 proved on the console that the Genesis plane shows through the 32X's through-bit colours
and that the commands work. REV4 moves the whole level there – Sonic CD's way of drawing:

| | R11 / REV1-3 | REV4, *Options → Draw: Genesis* |
|---|---|---|
| level tiles | drawn by both SH2s into a scrolling canvas | Genesis plane A (VRAM map 0xE000) |
| background | in the tiles (Fixed), redrawn (Moves) or plane B (REV3) | Genesis plane B (0xA000) |
| scrolling | 32X line table + canvas re-anchor (the hitch) | the Genesis' scroll registers |
| new tiles scrolling in | drawn by the SH2s | one map column (30 words) per 8 pixels, command 67 |
| the 32X | the whole picture | only what moves, on a fixed 320 × 176 sprite layer |

## The data (tools/stx_genesis.py, built by tools/stx_pack.py into every level's pack)
- **GENLVL.BIN** from the level's see-through version (LEVELG.BIN / COMPG.BIN: the background
  left out). Its pictures cut into 8 × 8 tiles, 4 bits a pixel, **3 palettes of 15 colours**
  (each tile picks the one that shows it best; colour 0 = see-through), identical and mirrored
  tiles once. Each picture's 2 × 2 map entries. The map as **columns** (2 per level cell,
  30 entries each), identical columns once. The **animations** (coins, water, the goal ...)
  as groups of tiles with the same speed and length: fixed tiles in VRAM, each frame's tiles
  uploaded over them when the frame changes. A gradient level carries its plane B too.
- **GENBG1.BIN** in each background picture's pack: the picture with **1 palette** (3) and at
  most 448 tiles (similar tiles merged), 256 × 192 as REV3's.
- VRAM: font 0x0000 (tile 0 = blank), background tiles 0x1000-0x47FF, level tiles 0x4800-0x9FFF
  and 0xB000-0xBFFF (832), plane B 0xA000, the boot text's plane 0xC000 (untouched), sprite
  table 0xD000 (empty), H scroll 0xD400, plane A 0xE000. Measured: a level needs 119-232 tiles
  plus 24-212 for its animations.
- Sizes: GENLVL.BIN 25-83 KB a level (2.5 MB for all). With REV3's see-through versions the
  packs grew, so area A (kept in the cart from start-up) now holds Icyisland levels 1-2.

## The game (game/gen_level.c; game/game.c gl_world; game/level.c)
- A level read with Draw: Genesis uses LEVELG.BIN (the rules are the same), GENLVL.BIN and its
  picture's GENBG1.BIN. Without them (the title's demo, a picture whose pack cannot be in the cart
  with the level) it is drawn by the 32X as before.
- First frame: display off, VDP registers for the layout above, palettes, tiles, plane B, the
  64 columns around the view, the animation frames, the scroll; display on.
- **After each flip** (the game loop, right after it waited for the 32X to show the new frame):
  the Genesis scroll registers are set to that frame's camera (plane A: the level; plane B: the
  background at the level's speed), so the two pictures change at the same vertical blank (a
  write a little late lands in the 32X's 12 border lines). Then columns coming near the view
  (at most 6 a frame: the view's 41 + 8 to the left, the rest to the right), cells whose picture
  changed (a coin taken: 4 map entries), animation frames that changed.
- **The 32X**: the canvas is a fixed sprite layer, colour 192 (= 0x8000, through) everywhere
  except what is drawn: the far snow and clouds with the level's tiles drawn again over them
  (they are behind the tiles), the bad guys and Tux with the foreground tiles over them, the near
  snow and scores. Each rectangle drawn is widened to the level's 16-pixel cells (+ 2 pixels:
  Vic's loops start on even x) and cleared to colour 192 when that buffer is drawn next; a frame
  for a menu or the intro clears the whole view next time.
- Leaving the level (map, menus): display off, md_init_hw's VDP registers back.
- REV3's *Background: Genesis* uses the same layout now (its plane A = its plane B's map).

## Checked on the PC
- The PC model of the Genesis chip (game/yt_host.c) now has both planes, the column command
  and the scroll table; frames are saved the way the console shows them (the 32X frame with the
  Genesis state set after its flip).
- All 77 levels, 900 frames of random play: Draw: Genesis against a reference that clears the
  whole sprite layer and writes every column again every frame - **identical** (two bugs found
  this way and fixed: sprites cut to under 8 pixels were not in the rectangle list, and the
  rectangles missed the pixel left of an odd x).
- All 77 levels, normal drawing against ST_TRUTH: as before (the same 11 levels, kept clouds).
- All 77 levels, 3000 frames, address / undefined-behaviour checkers, Draw: Genesis on.
- Side by side with the 32X-drawn level: the same positions frame by frame; the background now
  moves at SuperTux's own speed (half, in most levels).
- The SH2 build; tools/check_boot.py: the boot files plus exactly the added command blocks.

## The boot stack (one more command, tested like REV2)
- `cd32x/subcpu/hw_md.s`: 68000 command 37 `copy_vram_col` - copy_vram with the VDP's
  auto-increment at 128 (down a 64-cell plane column), then back to 2. `MD_CMD_END` 38.
- `cd32x/subcpu/main.c`: SH2 command 67 - COMM2 = VRAM address, COMM8L = (columns - 1) << 30 |
  words a column << 22 | cart offset; reply COMM12. A source crossing a 1 MB bank of the cart
  window is refused (the SH2 then writes those words one by one with 62).
- Nothing in the boot sequence calls it. **Run the disc in Fusion first** (it must reach the
  loading screen as REV2/REV3 did), then burn it.

## What to test on the console
1. *Options → Draw: Genesis* (left / right on Draw), then Icyisland level 1: the level, its
   background, Tux and the bad guys; running and jumping without a catch every 48-64 pixels.
2. Coins taken, bricks broken, boxes emptied: the cell changes; leave and come back to it.
3. Coins / water animate; dying and restarting; the world map and menus afterwards (nothing of
   the Genesis picture left over).
4. *Timing: Worst* with Draw: Genesis against Draw: CPU: X, T (tiles: near 0 now), S (sprites
   and clearing), H (here: the Genesis commands after the flip).
