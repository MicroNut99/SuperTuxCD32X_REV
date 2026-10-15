# REV7 – the 32X in front of the Genesis (REV6's black screen)

**Seen on the console with REV6:** a black screen with Tux and the snow as light outlines, the
level's ice only in a strip at the top and in the panel at the bottom.

**Why** (32X hardware manual, VDP bitmap mode register 0x4100, bit 7 PRI, in `docs/` of the docs
repo): PRI 0 = "MEGA Drive has priority (initial value)", PRI 1 = "32X has priority"; a 32X
colour with the through bit (0x8000) is shown on the *other* side of the Genesis picture. The
game always ran with PRI 0 - harmless while the Genesis display was off. With the Genesis
drawing the level and PRI 0:
- the Genesis planes were in front of the 32X;
- the 32X's see-through colour (192 = 0x8000) went in front of them - as black;
- Tux, the bad guys, the snow and the panel (normal colours) went behind the planes, so the
  level showed only inside their outlines.
The PC model composited the way the design needs (32X in front, through-bit colour behind), so
every PC check passed.

**Fix** (`game/gen_level.c`, game code - the boot path and `port/` are untouched,
`tools/check_boot.py` passes): PRI is set to 1 just before the Genesis display is switched on for
a level and back to 0 when the Genesis display is switched off (world map, menus). With PRI 1:
the 32X sprites and panel in front, the see-through colour behind the Genesis planes, which show.

Everything else as REV6 (the game built from Genesis tiles, no switch; REV5's saves).
