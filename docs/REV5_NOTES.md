# REV5 – REV4's fixes from the console

Reported on the console with REV4: *Options → Draw: Genesis* changed nothing you could see, and
saving never worked. No boot stack changes in REV5 (`tools/check_boot.py`: the same files and the
same four command blocks as REV4).

## Draw: Genesis
**What could go wrong in REV4:** a level was only put on the Genesis when two packs were in the
cart together: the level's own and its background picture's (GENBG1.BIN in OJ1nn.PAK). When the
picture's pack could not be kept with the level (`back_home`: the two overlap in the cart's ring),
the level quietly fell back to the 32X drawing it - looking exactly like Draw: CPU.

**REV5:**
- `tools/stx_genesis.py`: `picture_plane()` makes the picture's plane B (1 palette, ≤ 448 tiles,
  256 × 192 as REV3/REV4) and `genesis_level(..., picture=)` puts it **inside every level's
  GENLVL.BIN**. Header word 20 is now the plane B kind: 0 gradient (tied to the level's lines),
  1 picture (moves at the level's background speed). All 77 GENLVL.BIN have their plane B
  (61 pictures, 16 gradients); 3.4 MB for all, the packs still fit (area A: levels 1-2).
- `game/level.c`: Draw: Genesis needs only the level's pack (GENLVL.BIN, LEVELG.BIN, COMPG.BIN);
  it no longer reads the picture's pack from the disc at all. `gl_bg1` is gone.
- `game/gen_level.c`: plane B always from GENLVL.BIN; the kind word picks the scrolling.
- **The timing line** (Options → Timing On) now ends with:
  `DG` the Genesis is drawing this level · `DX` Draw: Genesis is on but this level was drawn by the
  32X (it should never show now) · `D0` / `D1` / `D!` as before (CPU / DMA rows / DMA failed).
- **Options:** the help line says `DRAW: NEXT LEVEL` - Draw and Background are applied when a level
  is read, so changing them in the pause menu shows from the next level read.

## Saves
REV4 and earlier kept the saves (options, high scores, the 7 game slots; 368 bytes, "STX2") in
the Sega CD's backup RAM with commands 56-59. That never worked on the console.

REV5 uses the RAM cart's save area at 0x3F0000 - the way OpenJazz CD32X and OpenLara CD32X save,
proven on the console (survives power-off):
- writing: each word into the Sub-CPU's 4 KB buffer (CMD 58, `plat_save_put_word`), then the buffer
  into the cart (CMD 61, `plat_save_piece`), which reads it back and replies 1 / 0;
- reading: straight from the cart at `0x223F0000 + piece × 4 KB` (cache-through);
- SuperTux uses **piece 13** (cart 0x3FD000). Pieces in use on this cart by the other ports:
  OpenJazz 0-4, Holiday Hare 5-9, the OpenJazz port's settings 10. The level packs end at
  0x3F0000 (`tools/stx_pack.py` AREA_B_END).
- A blank or foreign piece (not "STX2") gives the defaults, as before.

## Checked on the PC
- All 77 levels, 900 frames of random play, Draw: Genesis against the reference that clears the
  whole sprite layer and writes every column again each frame: **identical**.
- All 77 levels, 3000 frames, address / undefined-behaviour checkers, Draw: Genesis on: clean.
- Level 1 with Draw: Genesis: the picture on plane B, `DG` on the timing line.
- The SH2 build; `tools/check_boot.py`.

## What to test on the console
1. *Options → Timing: On*, *Draw: Genesis*, then start a level (from the map, not the pause
   menu). The second panel line should end in **DG**. If it says **DX**, tell me the level.
2. With DG: the numbers F, T (tiles: near 0), S, H, W, N against Draw: CPU on the same spot.
3. Saves: change an option, quit to the title, switch off, switch on: the option should still be
   set. Then a game slot: save on the world map, switch off and on, load it.
