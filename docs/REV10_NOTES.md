# REV10 – the exit igloo (and every other big picture), slower credits

**Seen on the console:** at the end of Icyisland level 1, a small piece of something floating
in the sky and no exit.

**Why:** SuperTux draws a tile's picture at its full size from the tile's cell. Most pictures are
one cell, but some are much bigger: the **exit igloo** (front 225 × 192, back 128 × 192 - at the
end of 43 levels), the tree in Bonus Island 2 level 19 (256 × 384), Nolok's statues (32 × 96),
the 128 × 128 background tiles, the 64 × 32 background piece. `tools/stx_convert.py` cut every
tile to one cell since R1, so only their top-left corners showed. R11 had the same gap; its
background pictures hid it.

**Fix** (`tools/stx_convert.py BigTiles`): each big picture is cut into cell-sized pieces that go
into the cells it covers, as new tiles (203; 499 tiles in all, the game takes 512). A covered
cell that already has a tile in the same layer gets one new tile holding both - the pieces under
it (SuperTux draws a layer row by row, so the cell's own tile is drawn later, on top) - with that
tile's rules (solid, coin ...). Empty pieces are left out. The Genesis level data, the 32X's
overlay drawing and the rules all use them like any tile.

**The credits** scrolled at 60 pixels a second - too fast to read on the console. Now 25
(`game/menus.c text_page`; up / down still scroll by hand).

**REV10b - the story** (the intro before a new game and in Options, and the end-of-world texts)
scrolled at 50 pixels a second; now 19, a little slower than the credits.

**The title's demo** used the Tux of the last game played (big or fire Tux), jumping into the
blocks over the start all the time - on the console it looked like Tux jittering. SuperTux's own
title resets the player (`player_status.reset()`): the demo's Tux is small again
(`game/game.c demo_start / demo_run`). The title lettering is not touched.

## Checked on the PC
- The exit igloo at the end of Icyisland level 1, the tree in Bonus Island 2 level 19.
- All 77 levels, 900 frames of random play, against the reference: identical; the title with
  its demo, 3000 frames: identical. All 77 levels, 3000 frames, address / undefined-behaviour
  checkers: clean. The SH2 build; `tools/check_boot.py`.

No boot stack changes.
