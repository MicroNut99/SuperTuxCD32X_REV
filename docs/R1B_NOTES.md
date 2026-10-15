# R1b - the first disc: world 1, Tux, Vic's tiler

**R1c** (after R1b built and was ready to burn): the diagnostics are off again - the boot path is
Hurrican's byte for byte, no `-DOJ32X_DIAG_TEXT=1`; the splash has SuperTux's logo, "LOADING" in
its gold letters and a panel around the boot path's progress bar (`tools/make_stx_splash.py`).
The disc was `SuperTuxR1c.cue`.

**R1d** - R1c on the console (first hardware test): the splash, the music (faint), then the red
error screen "SH2 EXCEPTION - ILLEGAL INSTRUCTION, PC 06000A26, PR 060008B4, last log MAIN()".
06000A26 is inside Vic's `draw_handle_layercmd`: a `0x0000` word in the middle of the function.
YATSSD puts its drawing functions in `.data` (they run from SDRAM); gcc at -O2 aligns loop
starts with `.align`, and in a `.data` section the assembler pads with zeros instead of NOPs -
the CPU runs into the padding on the first tile layer. Vic builds with `-fno-align-loops
-fno-align-jumps -fno-align-labels` (yatssd/Makefile "release"); mars/Makefile now builds his
files with his release flags, and `tools/check_sdram_code.py` (run by mars/Makefile) stops the
build if any SDRAM function still contains padding it can fall into - it finds 06000A26 in the
R1c program. The music tracks are now normalised to a peak of 0.70 (they peaked at 0.16-0.22:
faint). Disc was `SuperTuxR1d.cue`.

**R1e** - R1d ran on the console (two videos: level 1 at 20-30 frames a second; the timing line
said tiles about 19 ms, HUD 10.3 ms). Micronut99: "add everything - all the extra menu items of
the other projects". Added, after Hurrican CD32X's menus (game/p1.c P1b-P1p) and the OpenJazz
port's settings:
- the **title screen** (SuperTux's title picture and logo, `TITLE.BIN`): START GAME, OPTIONS;
  a new game from level 1 shows SuperTux's **story** (intro.txt) first
- **OPTIONS**: LEVEL (start level 1-26), LIVES (1-9), MUSIC on/off, TIMING (the timing line)
  on/off, BUTTONS (C or B jumps - the OpenJazz port's A/B swap), CONTROLS, STORY, HIGH SCORES,
  CREDITS (SuperTux's CREDITS, scrolling, with a music player: LEFT/RIGHT plays tracks 2-13),
  BACK (saves)
- **pause**: CONTINUE, RESTART LEVEL, MAIN MENU, TIMING on/off, MUSIC on/off
- the level's **name** when it starts, LEVEL CLEAR, GAME OVER, the **ending** (extro.txt)
  after level 26
- SuperTux's **score** (25 a coin) and a **high score table** of ten with name entry; the
  table and the options are kept in the Sega CD's backup RAM with Hurrican's commands 56-59
  (128 bytes at byte 3840 of the file's buffer, "STX1" - Hurrican's are the 128 after them)
- the letters are SuperTux's own (`FONTS.BIN`: letters-gold and letters-white at full size,
  letters-white-small for the texts; halved letters lost their white insides)
- the panel is drawn only when its text changes (it was 10.3 ms a frame); the timing line is
  off by default (OPTIONS / pause); otherwise the panel's second line is the score.
Not taken over: Hurrican's demo (attract) mode and its 2ND CPU switch (SuperTux's rules are
light; nothing runs on the second SH2 yet). Disc was `SuperTuxR1e.cue`.

**R1f** - Micronut99: put the options, the level choice, the credits and the extras under the
pause menu. The pause now has CONTINUE, RESTART LEVEL, **OPTIONS**, MAIN MENU, TIMING, MUSIC;
OPTIONS opens the same options page as the title's (LEVEL, LIVES, MUSIC, TIMING, BUTTONS,
CONTROLS, STORY, HIGH SCORES, CREDITS); its BACK (or B) returns to the level, still paused -
or, when LEVEL was changed, starts that level. Disc: `SuperTuxR1f.cue`.

## What it does

- The boot path is Hurrican CD32X's, unchanged. `tools/check_boot.py` passes: 70 files, with the 4
  diagnostics switches on.
- The diagnostics are on:
  - Chilly's white lines and the green lines show from power-on until the game starts.
  - `-DOJ32X_DIAG_TEXT=1` is passed through `EXTRA_DEFS`, so the text stays up until C++
    start-up has worked.
  - The platform layer switches it off just before `main()` and writes `STATE 4B10`.
- `main()` (`game/stx.c`):
  - Loads world 1 level 1 (`OJ001.PAK`, command 38 id 201, into the cart at `0x280000`).
  - Plays its CD track (track 3, Mortimers_chipdisko).
  - Runs Tux at 100 logic steps a second, SuperTux's own time step.
  - Draws with Vic's tiler.
- Buttons:
  - Left / right walk; **B** held runs; **C** or **A** jumps (hold for the full height).
  - **START** pauses. In the pause, left / right picks a level (1-26) and A or C loads it.
- Rules (SuperTux 0.1.3's, in fixed point):
  - Walk, run, skid, jump (5.2, or 5.8 when running), early release, the "pressed just before
    landing" jump.
  - Ice, coins (100 give a life), the goal or level end (the level-done track, 7 s, then the next
    level).
  - Falling out of the level costs a life, and so does the time running out.
- Not yet: enemies, bonus blocks and bricks, big Tux / fire Tux, animated tiles (they show their
  first frame), the background's half-speed scrolling, particles (snow, clouds).

## The screen (`game/yt.h`)

| Frame buffer byte | What |
|---|---|
| `0x00000` | line table (the platform layer writes it) |
| `0x00200` | **Vic's canvas**: 384 a line, 200 lines (Hurrican's ring area, used without wrapping) |
| `0x12E00` | the **panel**: 24 lines of 320 (HUD + timing line) - `plat_present_scroll`'s own panel |
| `0x14C00` | **Vic's dirty matrix** ("extrafb"; `canvas_yaw = 220` puts it here) |
| `0x16000` | the platform layer's blank line (border lines) |

`plat_present_scroll(palette, scroll_y, scroll_x, 176)` shows:

- 12 border lines;
- 176 lines of the canvas from (`scroll_x`, `scroll_y`);
- the 24 panel lines;
- 12 border lines.

**Why 176 lines of view:**

- Vic's tile loop draws whole 16-pixel tiles.
- The canvas must hold the view plus one tile row of room. That makes 192 lines, and only 200
  exist before the panel.
- Hurrican's view is 176 + a 24-line HUD band too.
- SuperTux showed all 480 lines (15 tiles); here 352 of them are in view. The camera follows Tux
  up and down only when he leaves the middle band (our rule; SuperTux 0.1.3 never scrolled
  vertically).

**The shift-register bug cannot happen.**

- `scroll_x` stays 0-63 and `scroll_y` 0-15.
- So every line-table entry is `0x100 + row*192 + scroll_x/2`, whose low byte is at most 223.
- The PC build checks every present and stops if an entry ever ends in `0xFF` while the shift is on.

## Vic's tiler under Hurrican's boot path

**Vic's files are unchanged** (`yatssd/`) and linked:

- `dtiles.c`: `draw_handle_layercmd` (the tile loop and the dirty matrix) and `draw_dirtyrect`.
- `dsprite.c`: `draw_sprite`.
- `draw.c` / `draw_inc.h`: the pixel loops and the scissor.
- `font.c` (the HUD font) and `sh2_fixed.s`.

Vic's code that lives in `.data` runs from SDRAM, as it does in YATSSD.

**Not linked or not called** (`hw_32x.c`, and `init_tilemap` / `draw_tile_layer` / `draw_tilemap`
in `dtiles.c`). These are the parts that talk to the hardware Vic's own way. Each one clashes with
the boot path:

| YATSSD does | Here | Why |
|---|---|---|
| `Hw32xInit`, its interrupts | the platform layer's start-up | the boot path sets up the VDP and the interrupts |
| `Hw32xUpdateLineTable`, `MARS_VDP_SHIFTREG = ...` | `plat_present_scroll(pal, scroll_y, scroll_x, 176)` | the platform layer writes the line table, and the shift at the swap (`OJ32X_FLIP_ASYNC`) |
| `COMM4 = 3`, then wait for the slave SH2 | only `COMM6 = 0` | our slave runs Hurrican's `plat_parallel` jobs, not Vic's loop. The master would wait for ever. With one CPU, `draw_handle_layercmd` draws every tile itself (its COMM6 counter just counts). |
| `HwMd*`: Genesis planes through YATSSD's own 68000 program | not used | our 68000 runs the boot path's listener (`hw_md.s`), which is never changed |

`game/yt_layers.c` is `draw_tile_layer` for one layer, the same arithmetic without those calls.
`game/yt_mars.c` holds the two registers and stubs for the `hw_32x.c` functions that `dtiles.c`
names. The stubs stop with the error screen if one is ever called.

**How the dirty matrix behaves** (Vic's rule, unchanged):

- Each frame buffer remembers which tile each canvas cell holds.
  - Cells whose tile did not change are skipped.
  - Cells with a tile on layer 1-3 (over the background) are redrawn every frame.
  - Sprites mark their cells.
- When the camera passes a multiple of 64 sideways or 16 up / down, the canvas re-anchors. Every
  cell whose tile differs is redrawn, which is nearly the whole screen because the background picture
  is a layer. Watch the `T` number on the timing line at those moments.
- `draw_dirtyrect` marks from line `y / 128` downward (it shifts `y` right by 3, then by 4 again).
  That is more cells than needed, never fewer. It is Vic's code and stays as it is.

**Vic's pixel loops:**

- They count down an unsigned "units / 4". A sprite with fewer than 8 pixels across after clipping
  would make about 4 billion writes (Sonic32 T7).
- `yt_sprite` therefore skips sprites with less than 8 x 2 pixels on screen.
- Sprites snap to even x (16-bit writes, Kobo rule 4).

## The timing line (panel, line 2)

`Fnn.n L t T t S t H t W t Nn` (F = frames a second). The times are in ms per frame, averaged over 16 frames:

| Field | Meaning |
|---|---|
| L | the logic steps |
| T | the tile layers |
| S | Tux |
| H | the HUD |
| W | waiting for the last swap |
| N | tiles drawn per frame |

## Packs (`tools/stx_pack.py`, formats in its header)

- `IMAGE8.RAW` is the resident pack (119 KB): palette, the 412 tile frames, the sprites.
- `OJ001.PAK` to `OJ026.PAK` hold world 1 (about 120 KB each). Each has the four layers in YATSSD's
  form, the rules layer, and the background as 16x16 tiles with its 64 colours. At about 30-40 KB/s
  a level loads in 3-4 s, with the splash and its bar.
- `SFX00.BIN` holds 12 SuperTux sounds (`tools/make_stx_sfx.py`, Hurrican's pack format).
- `IMAGE.RAW` is the splash: SuperTux's title background and logo (`tools/make_stx_splash.py`).

## Checked here (PC)

- `make -C host` builds the game on the PC.
  - `game/host.c` stands in for the platform layer; it is Hurrican's, plus the panel.
  - `game/yt_host.c` holds C copies of Vic's rules. They also stop if a write leaves the canvas.
- Level 1 frames were looked at: tiles, background, Tux walking and jumping, coins taken, the HUD
  and the timing line.
- `make -C host asan` ran 6000 frames of random buttons on levels 1, 6, 14 and 26 (with level
  changes from the pause) with no error.
- The SH2 program compiles and links here with sh-elf gcc 12.1 / binutils 2.42 / newlib 4.1,
  built from source as for Hurrican.
  - The program is 136 KB, with 11 KB copied to SDRAM, including Vic's pixel loops and tile loop,
    which run from SDRAM as in YATSSD.
  - It uses 54 KB of `.bss` and leaves 178 KB of heap.
  - `tools/prep_openlara_cd.py` accepts it: entries master `0x06000240`, slave `0x0600051C`,
    module name "OpenJazz 32X".
  - The linker's "cannot find entry symbol pri_start" warning comes from the unchanged
    `crt0_oj.s` / `oj.ld`, so Hurrican's link should print it too. It does no harm: the ROM header
    carries the entry addresses.
- The Sub-CPU / 68000 step needs the m68k toolchain, which is not here. That step is Hurrican's,
  unchanged.

## On the console: what to look at

1. **Boot.** These come first: the white lines, the green lines, the splash, the bar filling twice
   (the program, then `IMAGE8.RAW`), and then the bar for `OJ001.PAK`. If it stops, the `STATE:`
   row and the last green line say where.
2. **The first picture.** It should be level 1 with the panel below it. A black or striped view with
   the panel showing means the canvas or dirty matrix is wrong; a photo helps.
3. **The timing line** while walking, and at the re-anchor moments.
4. **The music** (track 3), and the jump / coin sounds.
