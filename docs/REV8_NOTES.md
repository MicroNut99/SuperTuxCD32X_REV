# REV8 – the sky is the 32X's (as in Sonic32X)

REV7 on the console: smooth, but the sky looked poor - it was on the Genesis' plane B with one
palette of 15 colours, at most 448 tiles, 0.8 × the size and dithered.

**REV8: the Genesis sky is switched off; the 32X draws a gradient sky**, the way Sonic32X
(MicroNut99/Sonic32XPlus1-RealHardware) gives Green Hill and the other zones 64-shade 32X skies.

- **The colours** (`tools/stx_pack.py sky_shades`): each level's 32X colours 192-255 are its 64
  sky shades, in 15-bit colour, each with the through bit.
  - A gradient level: SuperTux's own two colours, top to bottom.
  - A picture level: a gradient in the picture's own colours - the averages of its top, middle
    and bottom thirds joined smoothly (blue sky to sea to snow for the arctic picture, night to
    deep blue for the night sea, the castle's red between greys ...).
- **The layers:** during a level the 32X is in front of the Genesis (REV7, PRI 1); a colour with
  the through bit goes behind the Genesis planes. So the sky is behind the level's tiles and
  shows wherever they are see-through; Tux, the bad guys, the snow and the panel stay in front.
- **The drawing** (`game/gen_level.c sky_fill`): the gradient is fixed to the screen (as
  SuperTux's own gradient): view line y is shade y × 64 / 176. Clearing what the sprites covered
  last time puts the sky back line by line - the same work as clearing to the see-through
  colour before. No scrolling cost: the sky does not move.
- **Plane B:** an empty map (tile 0, see-through); no background tiles uploaded (`GENLVL.BIN`
  plane B kind 2). Genesis video memory 0x1000-0x47FF and palette 3 are free now.

## The PC model now follows the 32X manual
`game/host.c` composites as the manual's "Priority" says: PRI 0 the Genesis in front, PRI 1 the
32X; a through-bit colour goes to the other side; a see-through Genesis pixel shows what is
behind it. With REV7's priority switch left out, the PC frame is now exactly REV6's console
picture (black, Tux an outline, strips top and bottom) - the check that would have caught it.

No boot stack changes.

## Checked on the PC
- All 77 levels, 900 frames of random play, against the reference that clears the whole 32X
  layer and writes every column each frame: identical. The title with its demo, 3000 frames:
  identical.
- All 77 levels, 3000 frames, address / undefined-behaviour checkers: clean.
- The SH2 build; `tools/check_boot.py`.
