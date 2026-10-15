# SuperTux CD32X - handoff

Project folder: `S:\KoboPort\sega-toolchain-12.1\sega\SuperTuxCD32X`
(WSL `/mnt/s/KoboPort/sega-toolchain-12.1/sega/SuperTuxCD32X`), repository
github.com/MicroNut99/SuperTuxCD32X.

## Decisions (Micronut99, Oct 2026)

- **Target: SuperTux 0.1.3 ("Milestone 1", 2004).** The current SuperTux (0.6.x) is 136,000
  lines of C++ with OpenGL and Squirrel and 245 MB of data - out of reach. 0.1.3: 20,000 lines
  (1,900 of them the level editor), SDL 1.2, 640x480, 32x32 tiles, 16 MB of data.
- **A CD32X disc with the 4 MB RAM cart from the start** (not a cartridge first). The boot path,
  cart and Sub-CPU layers are Hurrican CD32X's (= the OpenJazz port's J19b, proven on the
  console), taken unchanged into `cd32x/` and `port/`.
- **Vic's tiler for drawing:** YATSSD (`yatssd/`, MIT): tile maps of any size, several layers
  with parallax, dirty rectangles, the hardware 1-pixel shift, both SH2s drawing, sprites.

## The boot path - never edited

Rule taken over from Hurrican (its first benchmark disc, with an edited boot path, gave a black
screen; with the boot path byte for byte as the last proven disc, it booted): `cd32x/`, `port/`,
`tools/make_oj_rom.py`, `tools/make_boot_image.py`, `tools/mars_boot.bin` are Hurrican CD32X's
(commit 37852a6), unchanged. New code runs only from `main()` and uses the platform layer's
existing calls. `tools/check_boot.py` (run first by build.sh) compares every one of those files
with `docs/BOOT_FILES.md5` (Hurrican's) and stops the build on any difference.

**Diagnostics: ON for the first builds (R1a, R1b), OFF from R1c** (Micronut99 after R1b built
and was ready to burn: "remove all the diags" - the splash with its loading bar does the job).
R1c's boot path is Hurrican's byte for byte; `check_boot.py` prints "release". To turn the
diagnostics on again (a disc that does not boot): change the four lines exactly as in
`tools/check_boot.py` SWITCHES (the "here" lines) and add `-DOJ32X_DIAG_TEXT=1` to EXTRA_DEFS in
build.sh - the check accepts either state. The switches (first builds on, R1c Hurrican's):

| Switch | File | Hurrican | Here | Shows |
|---|---|---|---|---|
| `OJ_DIAG_TEXT` | `cd32x/sh2/cdloader_sh2.c` | 0 | **1** | the loader switches the Genesis text on (CMD 52) and draws it in front of the splash |
| `KOBO_DIAG_TEXT` | `cd32x/subcpu/main.c` | 0 | **1** | the Sub-CPU's green text (STATE codes, file names, checkpoints) |
| `KOBO_BOOT_TEXT` | `cd32x/subcpu/main.c` | 1 | 1 | Chilly's white 32X start-up messages (already on) |
| `SCD_DIAG_TEXT` | `cd32x/subcpu/scd.c` | 0 | **1** | the green CKPT / INITCD lines and the CD error / root-directory lists |
| `OJ32X_DIAG_TEXT` | `port/plat_mars.c` (build flag `-DOJ32X_DIAG_TEXT=1` in mars/, R1b) | 0 | **1** | the text stays until the engine's C++ start-up has worked |
| Genesis display at the end of the 68000's set-up | `cd32x/subcpu/hw_md.s` `md_init_hw` | `0x8134` off (OL-13) | **`0x8174` on** | Chilly's white boot lines ("Activated 32X", "Cleared 32X", "32X Waiting", "CD BOOT TABLE" ...) from the very start, before the SH2 loader runs |

**The green lines - checked one by one** (Micronut99: "super important"). With the switches
above, every green line in the boot path is compiled in and drawn in a visible colour:

- the colour: `hw_md.s` sets palette line 1 to `0x00A0` (green; Kobo had it black for release);
- `main.c` `print_diag()` (all rows 0-27: build line, CD drive, Sub-CPU, 68K listener, 32X
  init and its errors, SH2 handshake, cart check, `STATE:` row 10, the Sub-CPU heartbeat row
  19, file and chunk lines): gated only by `KOBO_DIAG_TEXT` (now 1) and `quiet`, which is set
  only by command 44 when `SLIDESHOW_DEBUG` is 0 - it is 1, so never;
- the `#if KOBO_DIAG_TEXT` (OL-5) blocks - the heartbeat row 19 and the `STATE:` row 10 - are
  compiled in;
- `scd.c` `scd_diag_print`, `scd_diag_print_row2`, `scd_timeout_print`, `scd_print_row` (CKPT,
  INITCD, CD time-outs, file not found + the root directory list): gated only by
  `SCD_DIAG_TEXT` (now 1);
- the loader shows its codes (4D01-4D07, 4C00-4C3F, errors 4DE1-4DE4) through COMM12 in the
  `STATE:` row, and puts the Genesis text in front of the splash (`OJ_DIAG_TEXT` 1: DISPMODE
  0x81).

When they stop, by design of the boot path (not changed):

- `port/plat_mars.c` switches the Genesis text off (CMD 52) just before the game's `main()`,
  after the resident pack and the sound effects are loaded - the game's own screens follow;
- it then writes `STATE 4B10` (the game runs): the Sub-CPU stops reprinting rows 10, 18 and 19
  (OL-2b / HW5: reprinting every pass dropped Kobo to 1-2 fps, each line goes through the 68000).

In the game itself, problems show on the 32X side: the error screen (`plat_fatal`, with the last
log line) and the game's timing line.

**Why the white text is on (Micronut99):** for the first builds, so that a disc that stops
early is not a black screen - the white lines show how far the boot got. Later builds turn it
off again when the splash screen takes over that job: the splash is what tells the player the
system is working while a slow boot loads. Not every game boots equally fast - timing and
what the player sees while waiting matter (a black screen with no sign of life causes anxiety).
For a release: `0x8134` here (OL-13) and the other switches back to Hurrican's values, with the
splash on.

## Facts that shape the design

- **No floating point, as in Hurrican:** SuperTux 0.1.3's physics are float (`physic.cpp`,
  `player.cpp`); the SH2 has no FPU. Hurrican's lesson: porting the C++ objects ran at 4-5 fps.
  So the rules are rebuilt in C, fixed point, in SuperTux's own units, checked on the PC.
- **C++ standard library:** `ls /opt/toolchains/sega/sh-elf/sh-elf/lib/ | grep stdc++` printed
  nothing on Micronut99's PC, but Hurrican's `mars/Makefile` links `-lstdc++` (the multilib
  folders hold it). Not needed if the game is C.
- **The RAM cart is read-only for the SH2** (`port/pack.h`: "The SH2 cannot write the cart",
  OpenLara lesson 1): it is a 4 MB store filled from the disc through the 68000 (commands 38 +
  46, `plat_load_pack`, `plat_cart_write`), not extra working RAM. Working memory is the 256 KB
  SDRAM. Everything is prepared on the PC in the form the 32X draws from.
- **Screen height:** levels are 15 tiles = 240 lines at half size, the screen 224: the camera
  scrolls the last 16 lines vertically (the previews show the bottom 224 lines).
- Levels hold tile ids up to 295 (and one 2560, a bad id in one level); 188 different ids are
  used in all - a level map can become u8 through a table.
- The "-fast" modules are real faster versions (not copies): separate CD tracks.

## R1a (this round) - measured on SuperTux 0.1.3 (tag v0.1.3)

`./build.sh` -> `build/data/REPORT.TXT`:

| | |
|---|---|
| tiles | 296 ids, 412 frames 16x16 = 105,472 bytes |
| global colours | 191 (1-191); 192-255 the level's background |
| sprites (shared + small status pictures) | 189 pictures, 53,470 visible pixels after halving |
| backgrounds | 7 pictures 320x240 in 64 colours = 76,800 bytes each |
| levels | 77; 3.0 MB as stored now (3 layers u16), 101 KB compressed (zlib) |
| objects | snowball 661, mrbomb 508, stalactite 504, mriceblock 492, spiky 440, bouncingsnowball 350, flame 347, flyingsnowball 312, money 245, jumpy 125, fish 83 |
| music | 12 tracks, 15 min in all (Mortimers_chipdisko 158 s, theme 96 s, SALCON 90 s, cave 77 s, fortress2 67 s, fortress 45 s, the fast versions shorter, leveldone 8 s) |

Previews checked: all 77 levels draw (tiles, backgrounds, gradients, objects marked).

## R1b - the first disc (docs/R1B_NOTES.md)

- `game/stx.c`: world 1 (26 levels, `OJ001.PAK`-`OJ026.PAK`), Tux walking / running / skidding /
  jumping with SuperTux 0.1.3's rules in fixed point at its own 100 steps a second, coins, the
  level end, lives and time, the HUD and the timing line in a 24-line panel, pause with level
  choice.
- Drawing: Vic's tiler (`yatssd/` unchanged: `draw_handle_layercmd` + dirty matrix, `draw_sprite`,
  the pixel loops, the font) in Hurrican's frame buffer layout; `game/yt_layers.c` is YATSSD's
  `draw_tile_layer` without its three hardware calls (line table / shift register, the COMM4
  slave hand-shake, the Genesis planes), which cannot run under this boot path - R1B_NOTES.md
  says why, call by call.
- Build: `build.sh` = Hurrican's disc steps with SuperTux's data; `mars/Makefile` = Hurrican's
  with `game/` + Vic's files; `EXTRA_DEFS="-DOJ32X_MUSIC=1 -DOJ32X_DIAG_TEXT=1"`.
- Checked on the PC (`host/`, frames looked at, address/UB checkers with random input) and
  compiled + linked for the SH2 here; not yet run on the console.

## R1c-R1f

The splash, music levels, the menus and extras of the other ports, options from the pause
(docs/R1B_NOTES.md has a section for each).

## R2 - the whole game (docs/R2_NOTES.md)

- Micronut99: "build out the entire game, don't leave anything out"; size: **"Sharpened"** (half
  size + unsharp mask) - chosen over 3/4 after the 3/4 data was made.
- `game/stx.c` is replaced by seven files (`game/stx.h` lists them): title with demo, slots,
  world maps, all 77 levels with every bad guy and power-up, texts, high scores, saves.
- Screen layout unchanged from R1 (yt.h); `yt_mark_tile` added for animated tiles.
- `tools/stx_convert.py` / `stx_pack.py` rewritten (all levels, world maps, sprites, fonts,
  pictures, texts); PALETTE.BIN now holds entry 0 too (R2's first data had every colour one off).
- Saves: bytes 3072-3439 of the backup RAM file ("STX2").

## R3 - frames and the cart (docs/R3_NOTES.md)

- R2 on the console: F 17-22, T 22-29 ms, N 140-245, H 7-9 ms, L 6.5-9.5 ms.
- Merged layers (stx_pack.py), one tiler layer; variants for map changes; animated pictures;
  foreground / back-particle overlays (game.c overlay_cells).
- yt_sprite: Vic's clip + pixel loop, exact dirty cells (his draw_dirtyrect over-marks).
- Both SH2s: draw_handle_layercmd through plat_parallel (Vic's COMM6 counter shares the cells).
- Panel: drawn on change only, both shifts prepared, 32-bit copy.
- Hot code in SDRAM (mars/Makefile HOT_OBJS).
- Cart homes: DEMO.PAK in IMAGE8, area A (IMAGE9.RAW), area B ring (padded OJnnn.PAK);
  cart_load sends commands 38 + 46 itself (no splash re-read).

## R4 - re-anchor and moving background (docs/R4_NOTES.md)

- reanchor(): the canvas's pixels and the dirty matrix move with the anchor (extrafb words 1-2
  hold each buffer's top_h / top_v); only the new strip is drawn.
- Options - Background MOVES (opt_parallax, b[5] bit 1): BACK.BIN packs OJ100-OJ106 (fid 300+n),
  HOMES.BIN `back`; game.c draws yt_back + layers 1-2 as sprites + layer 3.
- yt_sprite: clipped sprites under 8x2 drawn byte by byte.

## R5 - plain backgrounds, fewer cells (docs/R5_NOTES.md)

- stx_convert.py SHARPEN_BACKGROUNDS False; sprites trimmed (SPRITES.BIN "STXT", dx/dy).
- yt_layer: the master lists the dirty cells, both SH2s draw half each (no COMM6 lock per cell);
  YT_VIC_TILER brings back draw_handle_layercmd.
- Clouds kept per buffer (game.c clouds_before / clouds_draw, yt_keep / yt_watch).
- level_mark_animated: per-buffer frame of each animation (anim_seen), whole canvas.
- Far snowflakes skipped over tiles (no overlay pass for snow).
- PC: ST_STATS, ST_NOPARTICLES.

## R6 - smooth motion (docs/R6_NOTES.md)

- R5 on the console: F 47-60, W 5-10 ms, but jerky: 100 Hz steps on a 60 Hz screen (2,2,1).
- interp_save before each step (camera, Tux, particles; objects_interp_save), step_clock sets
  interp_a; game_draw_world draws at lerp_fx(prev, now) and puts the positions back. wmap.c the
  same for map Tux.

## R7 - up / down re-anchor (docs/R7_NOTES.md)

- R6 on the console: F 33-40 / T 11-15 while the view went up and down (one-SH2 vertical moves).
- reanchor: vertical moves split by columns on both SH2s; cell_us16 / move_us[] measured, the
  cheaper of move and full redraw chosen (re-sampled every 16th). PC: ST_REANCHOR=move|draw.

## R8 - jumping with A (docs/R8_NOTES.md)

- Console: A gave a tiny jump, C a full one (same bit test in the game -> the A bit drops out
  on the console). read_input: jump let go only after 2 reads without it; panel shows P+hex pad
  when TIMING is on.

## R9 - stuck buttons, diagnostics (docs/R9_NOTES.md)

- Console: buttons stuck in R8, better with TIMING on (slower frames). PC: all fine.
- Panel line 2 with TIMING: P<pad hex> S<old pad replies> (COMM10 counter unchanged after a
  refresh). OPTIONS - MOTION SMOOTH/STEPS (opt_smooth, b[5] bit 2 = STEPS).
- Next: read S from the console. S climbing -> the pad's way (port / Sub-CPU: CMD 48 at 60 Hz);
  S 0 -> look at the game side again.

## R10 - ring, own record (docs/R10_NOTES.md)

- R9 on the console: pad fine (S 0); SMOOTH better; "background doesn't keep up" sometimes.
- yt_layers.c rewritten round a per-buffer record of 8-line bands in SDRAM (Vic's dirty matrix
  no longer used); OPTIONS - SCROLL RING (canvas line = world line % 200, no up/down re-anchor)
  / CLASSIC; per-buffer free sideways anchor (early 3-cell shifts on quiet frames); leave-in-place
  re-anchor keeps the record. yt_epoch / kept flags / drawn flags for the clouds.
- Fixed: diagonal move half order (R4-R9 strip). PC: ST_TRUTH=1 (reference), ST_CLASSIC=1.

## R11 - level select in Options (docs/R11_NOTES.md)

- R10 on the console: RING smoother. OPTIONS - LEVEL SELECT (only from the title).

## Next

1. R10 on the console: RING vs CLASSIC while jumping; the sideways catches; clouds.
2. The platform's frame pacing (port/plat_mars.c OJ32X_PACE, median of 16) holds fast frames
   back when about half run long; a compile switch, but port/ stays as proven unless the user
   asks.
3. If L is large: the bad guys' collision loops (all pairs, every 10 ms step).
4. MOVES could be faster by merging layers 1-3 into one picture a cell drawn keyed over the
   background, with a dirty mask for the cells under no tile.
