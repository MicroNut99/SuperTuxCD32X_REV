# R2 - the whole game, half size, sharpened

(R3 changed the drawing, the packs and the loading: docs/R3_NOTES.md.)

Micronut99 asked for "the entire game, don't leave anything out", and chose **"Sharpened"** from
the four test pictures (half size, half size sharpened, 3/4, full). R2 is SuperTux 0.1.3 from the
title screen to the end credits. Not yet run on the console.

## What is in it

| SuperTux 0.1.3 source | Here | What |
|---|---|---|
| `title.cpp` | `game/menus.c` | the title: `misc/menu.stl` playing behind the logo with Tux walking and jumping by himself (`demo_run`); START GAME, BONUS LEVELS, OPTIONS, CREDITS (+ HIGH SCORES) |
| `setup.cpp` (menus) | `game/menus.c` | the five save slots; Bonus Island I / II; options; the pause in a level (CONTINUE, OPTIONS, ABORT LEVEL + ours: RESTART LEVEL, MAIN MENU); the world map's menu (CONTINUE, OPTIONS, QUIT GAME) |
| `worldmap.cpp` | `game/wmap.c` | Icyisland and the two Bonus Islands: paths, one-way paths, stop and auto-walk tiles, "can't pass an unsolved level except back", teleporters (visible and invisible) with their messages, messages for walking over a place, level dots red / green, auto-walk after a level, extro + credits after the last level, Tux's map picture by his bonus, the save |
| `gameloop.cpp` | `game/game.c` | the level intro (name, TUX x lives, by author), time (hurry-up music under 20 s, TIME'S UP), the end sequence (goal tiles: walk on / stop; the level end; 7 s of leveldone), dying, the nearest reset point, game over screen |
| `world.cpp` | `game/game.c` | order of the actions, the camera (back scrolling with its 2 s turn, autoscroll levels), bricks (coins in bricks: 5, then empty), bonus boxes (coin, egg / ice flower, herring, 1-up), coins, bumping bricks |
| `player.cpp` | `game/tux.c` | small / big / fire Tux, walking, running, skidding, ice, jumping (higher when running, the early release, the jump pressed just before landing), ducking, shooting, kicking and carrying Mr. Iceblock, the herring's invincibility (star), the safe blink after a hit, dying |
| `badguy.cpp` | `game/objects.c` | all eleven: Mr. Iceblock (flat, kicked, carried, bounces off walls breaking bricks), Jumpy, Mr. Bomb and its bomb (ticking, explosion), the stalactite (shakes, falls), the flame (circles), the fish (jumps out of water), bouncing / flying snowballs, Spiky, the snowball; stay-on-platform; squishing, bumping from below, killing each other |
| `special.cpp`, `gameobjs.cpp` | `game/objects.c` | upgrades (rising out of the box, walking, bouncing), bullets (two at a time), bouncy coins, bouncing bricks, broken brick pieces, floating scores |
| `particlesystem.cpp` | `game/game.c` | snow (32 flakes, see below) and clouds |
| `text.cpp` | `game/menus.c` | the story, the three extros, the credits: scrolling text over SuperTux's pictures |
| `high_scores.cpp` | `game/menus.c` | the table of ten, with a name |

Counts: 77 levels (Icyisland 26, Bonus Island I 22, Bonus Island II 28, the title's level),
3 world maps, 20 sound effects, 12 CD tracks. **LEVEL SELECT** (ours, under BONUS LEVELS) plays
any of the 76 levels on its own.

## Buttons

| | |
|---|---|
| LEFT / RIGHT | walk |
| B (held) | run, carry Mr. Iceblock, shoot (fire Tux) |
| C or A | jump (held: higher) |
| DOWN | duck (big Tux) |
| START | pause menu |
| world map | the pad walks, A or C enters a level / a teleporter, START its menu |
| menus | UP / DOWN, A / C / START select, B back; in OPTIONS LEFT / RIGHT change |

OPTIONS - JUMP swaps A and B (C/B jump, A runs).

## The size: half, sharpened

`tools/stx_convert.py` halves every picture (premultiplied Lanczos: no dark fringes) and then
runs an unsharp mask (`SHARPEN = (radius 1.0, 110 %, threshold 2)`, PIL `ImageFilter.UnsharpMask`)
on tiles, sprites, backgrounds and the menu pictures. The fonts stay at SuperTux's own size.

Because it is half size, R2 keeps R1's proven screen layout (`game/yt.h`): 16-pixel tiles, a
320 x 176 view, the 24-line panel, the canvas re-anchored every 64 columns / 16 lines, the dirty
matrix at 0x14C00. The view is SuperTux's whole width (640 units) and 352 of its 480 lines;
the camera follows Tux up and down (`camera_y` in `game/game.c`: a band of 96 / 112 units).

3/4 size was built first and dropped before any disc (not in the history). To try it again:
`SCALE_N, SCALE_D` and `TILE` (24) in `stx_convert.py` / `stx_pack.py`, and in `game/yt.h` a
320 x 168 view with a 32-line panel, `YT_TILE` 24, `YT_SCROLL_X` 48, `YT_SCROLL_Y` 24, `YT_YAW`
228 (the dirty matrix at 0x15800); `px_of()` and `VIEW_*_U` in `game/stx.h`.

## Memory and packs

- Resident `IMAGE8.RAW` (507 KB, mounted before `main()`): palette, tiles (412 frames), 90
  sprites (182 pictures), fonts, the three menu pictures, the world map tiles, the world maps,
  the level names and the texts.
- `OJ001.PAK`-`OJ077.PAK` (one a level, 80-280 KB): `LEVEL.BIN` (header "STXR", four tile layers
  for the tiler, the rules layer, bad guys, reset points) and `BACKT.BIN` (the background cut
  into tiles + its 64 colours). Only one is in the cart at a time; a read takes 2-7 s, with the
  level's name on the screen. Going back to the title reads `OJ077.PAK` (its demo level) again.
- SDRAM: about 143 KB of the 238 KB below the stacks (the interactive layer and its rules
  2 x 31 KB for the widest level, 320 bad guys 20 KB, the world map 16 KB).

## The saves (backup RAM)

Hurrican's backup RAM file and commands (56-59), unchanged. SuperTux R2 uses bytes 3072-3439
("STX2"): options, the high-score table, slots 1-5 and one slot for each Bonus Island (`menus.c`
top comment has the layout). R1's 128 bytes at 3840 are not read any more (R1's options are
lost once). Hurrican's 3968+ is not touched. On the PC: `STXSAVE.BIN` in `ST_DIR`.

## Where the time goes (things to watch with OPTIONS - TIMING)

The timing line in the panel: `F` frames a second, then ms a frame for `L` the rules, `T` the
tile layers, `S` the sprites, `H` the panel, `W` the wait for the swap, `N` tiles drawn.

- Cells with tiles on two or more layers are redrawn every frame (Vic's dirty matrix keeps one
  number a cell); a sprite makes the cells under it redraw in the next frame.
- Snow: SuperTux has 128 flakes over twice the screen; R2 has 32 (`NSNOW`). Clouds: 15.
- Animated tiles (coins, water, the goal): their cells are marked in each buffer when the frame
  changes (every 25-50 ms) - `level_mark_animated`.
- The rules run at SuperTux's 100 steps a second whatever the frame rate (at most 10 a frame),
  so a slow frame rate slows nothing down but makes the motion jumpier.

## Not as in SuperTux (and why)

- The background picture scrolls with the level (SuperTux: at `bkgd_speed`, often half speed).
  One more layer would cost a second full redraw of most cells.
- 32 snowflakes instead of 128 (see above).
- No level editor (SuperTux's "Level Editor" menu item): it needs a mouse and a keyboard.
- No "Quit" in the main menu (a console).
- The credits use the theme (SuperTux plays credits.ogg, which 0.1.3 does not ship as a module).

## Checked here

- PC build (`host/`): every one of the 77 levels for 3000 frames with random input under the
  address / undefined-behaviour checkers (`make -C host asan`), the title / slots / story /
  world map / level / end / back-to-map path, level completion and auto-walk to the next level,
  fuzzed menus (six seeds, 12000 frames each). No errors.
- SH2 build and link: `.sdcode` + `.data` 10.7 KB, `.text` 161 KB, `.bss` 141 KB;
  `tools/check_sdram_code.py` passes; `tools/check_boot.py`: "release", 70 files identical.

PC checks (host only, never on the console): `ST_LEVEL=n` plays level n, `ST_MAP=m` a world map,
`ST_X=units` puts Tux there when a level starts.
