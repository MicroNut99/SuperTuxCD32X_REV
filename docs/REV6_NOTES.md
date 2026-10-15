# REV6 – the game built from Genesis tiles, no switch

REV4 and REV5 kept two versions of every level on the disc - R11's 32X-drawn one and the Genesis
one - and *Options → Draw* picked which was read. That was wrong for what REV is: the game is to
be **built** from Genesis tiles, as Sonic CD is. REV6 does that:

- **Every level is Genesis tiles** - all 76 levels and the title's demo. The level's tiles and
  its background are on the Genesis' planes A and B, scrolled by the Genesis chip; the 32X draws
  only what moves (Tux, the bad guys, bullets, coins in flight, snow, clouds, scores).
- **The 32X version of the levels is gone from the disc:** a level's pack is now only
  LEVELG.BIN + COMPG.BIN (the rules and the pictures the 32X draws for the moving things) and
  GENLVL.BIN (the Genesis tiles, map columns, animations, plane B). No LEVEL.BIN / COMP.BIN, and
  no background picture packs (OJ1nn.PAK) at all.
- **No switch:** *Options → Draw* and *Options → Background* are gone. Old saves with those bits
  are read and the bits ignored.
- `game/level.c`: every level read is GENLVL.BIN; a pack without it stops with
  `GENLVL.BIN: NO GENESIS LEVEL` (it cannot happen with this build's data). R4's picture-pack
  lookup (`back_home`) is removed.
- The title's demo: the Genesis scroll is set after each flip in the menus too
  (`menu_choose`, `message_screen`), and its map is written again when Tux goes back to the start.
- The timing line ends in `DG` in every level.
- Fixed (found by the PC check): after Tux died, the level's intro screen could show the
  Genesis map half from the old camera position (only 6 columns a frame were written at the
  new one). A restart now writes every column at the new camera.

## Checked on the PC
- All 77 levels, 900 frames of random play, against the reference that clears the whole 32X
  layer and writes every column each frame: identical. The title with its demo, 3000 frames:
  identical.
- All 77 levels, 3000 frames, address / undefined-behaviour checkers: clean.
- The packs: 9 MB instead of 25.7 MB; the cart now keeps Icyisland levels 1-7 from start-up
  (was 1-2), so fewer disc reads.
- The SH2 build; `tools/check_boot.py`.

Kept: the world maps, menus, pictures and text are the 32X's as before (they are not levels).
*Options → Motion* and *Scroll* stay (Scroll is the world map's).

No boot stack changes (same as REV4/REV5).

## What to test on the console
1. The title: the demo level behind the logo should be the Genesis-tile version (Genesis colours:
   3 palettes of 15, slightly different shades from before).
2. Icyisland level 1: running and jumping; the timing line (Options → Timing: On) ends in `DG`.
3. Saves (REV5's cart save area): change an option, switch off and on.
