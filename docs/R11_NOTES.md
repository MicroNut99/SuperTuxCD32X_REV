# R11 - level select in Options

(R10: docs/R10_NOTES.md.) Micronut99 on R10: "RING seems to be smoother. I need a level select
on the options screen".

- OPTIONS - LEVEL SELECT (game/menus.c options_page): the same list as BONUS LEVELS - LEVEL
  SELECT (all 76 levels: 1-1 .. 1-26 Icyisland, B1-, B2- the Bonus Islands), the level played
  on its own (play_alone, a high score offered after it), then back to the title (its demo
  starts again).
- Only when OPTIONS is opened from the title: from a level's pause menu or the world map's menu
  it would start a level inside another one, so the line is not shown there.
- Nothing else changed from R10 (RING stays the default).

Checked here: the PC through title - OPTIONS - LEVEL SELECT - 1-2 into the level, its pause menu
and options (address / undefined-behaviour checkers); SH2 build, check_sdram_code.py,
check_boot.py (release, 70 files identical).
