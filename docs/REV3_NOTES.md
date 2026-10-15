# REV3 – the first picture on the Genesis

REV's goal is Sonic CD's way of drawing (README.md). REV2 added the commands and proved them to
boot on the console (15 Oct 2026). REV3 is the first use: a level's background picture on the
Genesis' plane B, the level drawn by the 32X on top of it.

## How the two pictures combine (32X hardware manual, "Priority")
- In the game the 32X is in front of the Genesis (DISPMODE PRI 0, as the platform layer leaves it).
- Every 32X colour has a **through bit** (bit 15). A pixel whose colour has it is shown *behind*
  the Genesis: where the Genesis has a pixel (not colour 0), the Genesis pixel shows.
- So the level's pictures are built a second time with the background left out
  (`LEVELG.BIN` + `COMPG.BIN`, tools/stx_pack.py): where the picture would show, colour 192, and
  in that version all of colours 192-255 are `0x8000` (through bit, black). The platform layer
  passes them on unchanged (it only turns `0x0000` into `0x0400`).
- The Genesis display is switched on only while a level with the picture is on screen.

## The picture on the Genesis (tools/stx_genesis.py → GENBG.BIN in the picture's pack)
- 256 × 192: 0.8 × the 32X's size, so two copies side by side fill the 512-pixel plane and
  it wraps round with the picture: scrolling sideways never needs new map entries.
- 32 × 24 cells of 8 × 8, 4 palettes of 15 colours, 3 bits a channel (the Genesis' 512 colours),
  a 4 × 4 ordered dither for the sky gradients; identical (also mirrored) cells once:
  234-768 tiles a picture (VRAM 0x1000 up, below the sprite table at 0xA800).
- VRAM as the 68000 listener's md_init_hw sets it: plane B map 0xE000 (64 × 32), H scroll table
  0xAC00 (plane B's word at 0xAC02), plane A 0xC000 holds the green / white boot text - while the
  picture shows, plane A is pointed at 0xB000 (the window map, never written: empty) and back
  afterwards. No VRAM is cleared and no boot text is lost.

## The game (game/game.c gen_frame / gen_present, game/level.c, game/yt_mars.c)
- *Options → Background*: Fixed / Moves / **Genesis** (saved, backup RAM byte 5 bit 6). From
  the next level read on, like Moves. Levels with a gradient instead of a picture, and the title's
  demo level, stay as before.
- First frame of such a level: the Genesis display off, colours (CMD 64), plane map and tiles
  (CMD 63 from the cart), plane A to 0xB000, display on (CMD 65).
- Every frame: plane B's horizontal scroll = -(camera × the level's background speed × 0.8)
  (CMD 62 to 0xAC02, only when it changed); vertical: the level's height mapped onto the 16 lines
  the 192-line picture can move under the 176-line view (CMD 66, only when it changed). This is
  SuperTux's own parallax (the background at half speed in most levels) - Fixed draws the picture
  moving with the level, Moves redraws all of it every frame.
- First frame of anything else (map, menus, loading): display off, plane A back.
- The Genesis scroll is written when the 32X frame is handed over, not at the vertical blank: the
  picture can be a frame ahead of the level for part of a frame (at half speed: about a pixel).

## What it saves (PC, cells the 32X draws a frame)
Same as Fixed in a normal frame (the cells that scroll in still have to be drawn). The difference
is the **sideways shift**: when the canvas is left in place and the differing cells are drawn
(the R7 / R10 choice the console measures), far fewer cells differ - the sky is one picture:

| Level | worst frame, Fixed | worst frame, Genesis |
|---|---|---|
| 1 Welcome to Antarctica | 210 | 69 |
| 4 | 221 | 97 |
| 31 | 197 | 78 |
| 11 (gradient: no picture) | 87 | 87 |

So on the console the shift should mostly stop moving the 76 KB canvas and draw a few dozen
cells instead - the hitch the timing line's Worst shows as `M` / `ML`.

## Checked on the PC
- The PC build has a small model of the Genesis video chip (game/yt_host.c) and puts plane B
  behind every through-bit pixel of the saved frames.
- All 77 levels, Genesis on, against ST_TRUTH: the same 11 levels differ by the same pixels as
  without it (kept clouds / far snow, R10); with Draw: DMA and the shift redrawing too.
- All 77 levels, 3000 frames, address / undefined-behaviour checkers, Genesis + DMA + Timing
  Worst, RING and CLASSIC.

## Costs
- The packs: each level with a picture holds its see-through version too (+4.3 MB on the disc);
  IMAGE8.RAW grows by the demo level's (it never uses it, but the pack is built the same way), so
  area A now holds levels 1-3 (R11: 1-4) - level 4 is read from the disc like the others.
- Picture quality: 60 colours instead of 64 of the 32X's 32768; dithered skies.

## What to test on the console
1. *Options → Background: Genesis*, then a level (Icyisland level 1): the picture behind the
   level, moving at half speed; the level and Tux on top; no green text visible.
2. The world map and the menus afterwards: no Genesis picture left over.
3. *Timing: Worst*, running right: `M` / `ML` and `X` against Background: Fixed.
