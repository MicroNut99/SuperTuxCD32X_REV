# SuperTux CD32X REV – the rip and rebuild

> **REV is a rip and rebuild.** SuperTuxCD32X (R1–R11) got the whole game running on the Sega CD32X.
> REV takes that game apart and rebuilds how it draws, until it runs at the speed Sega CD games
> reach on the same hardware: a steady 60 frames a second, with no catch when the view scrolls.
> **The idea being tested is Sonic CD's:** the level goes on the Genesis video chip's tile planes,
> which scroll by register and cost no drawing at all, and the 32X draws only Tux, the bad guys and
> the effects on top (as Sonic 32X Plus does). R11 is the starting point (commit `0a38bbf`: the R11 source, unchanged). It runs
> at 45–60 frames a second on the console, with a hitch at every sideways shift of the picture
> (every 48–64 pixels). Nothing in REV is kept because "it was there in R11": every part
> of a frame is measured on the console (Options → Timing) and replaced if something faster
> exists.
>
> **The boot path.** The CD boot sequence itself (`cd32x/sh2/`, `port/`, `tools/make_oj_rom.py`,
> `make_boot_image.py`, `mars_boot.bin`) stays byte for byte as proven. The Genesis planes need
> commands the old stack did not have, so REV2 ADDS them to the Sub-CPU program and the 68000
> listener - new command numbers that only run when the game sends them, nothing in the boot
> sequence changed. `tools/check_boot.py` accepts exactly those added blocks and still stops the
> build on any other byte. Each such change is first tested in Kega Fusion with the green and white
> boot diagnostics on (Fusion boots CD32X discs up to the splash), then on the console.

## Release: REV10b

**What this release is:** SuperTux 0.1.3 on the Sega CD32X, rebuilt the Sonic CD way: every
level is drawn by the Genesis video chip from Genesis tiles, the 32X draws only what moves and
the sky. It runs on real hardware (Sega CD + 32X + 4 MB RAM cart) with the whole game: all 76
levels, both Bonus Islands, CD music, saves in the RAM cart (REV5 - confirmed working on the
console, 16 Oct 2026). On the console it is clearly smoother than R11, with no hitch when the view scrolls.
The method is described in detail below.

**Not yet done: profiling** on the console (*Options → Timing*). The "Frame rate" figures below are R11's.

## REV11 test builds: smoother motion and the 16 MB cart

Two test builds on top of REV10b. Each build's name is on the title, the disc, the zip and the git
tag (`STX_VERSION` in `game/stx.h`):

| Build | Command | Cart | Title |
|---|---|---|---|
| **REV11-T2** | `./build.sh` | 4 MB (the normal disc) | `SUPERTUX 0.1.3 - CD 32X - REV11-T2` |
| **REV11-16MB-T1** | `CART_MB=16 ./build.sh` | 16 MB (MicroNut99/16MB-PCB-CD32X); also plays on 4 MB | `SUPERTUX REV11-16MB-T1 - CART 16 MB` |

### What was done
- **Even motion (REV11-T1, in both builds).** Tux stood still on the screen, but the level behind
  him moved 4, 4, 3, 5, 3 px a frame instead of an even 4, 4, 4, 3 - seen as Tux jittering in the
  title demo. The game read its clock whenever the loop got to it, after work that takes a
  different time each frame (the Genesis columns after a frame swap). It now notes the time right
  after each swap (`game_wait_flip`, game code only). Measured in ares: 5 px jumps in 10 s of the
  demo 26 → 0, in Welcome to Antarctica 18 → 0.
- **The whole game in the 16 MB cart (REV11-16MB-T1).** All 70 level packs (9.3 MB) have their
  own place for good in pages 1-7 of the paged cart, packed largest first so all of them fit
  (`tools/stx_pack.py --cart-mb 16`). On the first start the game copies every level from the
  disc into the cart (`game/cart.c cart_preload`). The size probe finds 4, 8 or 16 MB at every
  start; the cart page is switched with command 68 (below).
- **Found and checked in the emulator, not by console profiling:** MicroNut99/ares_android's
  headless runner models the RAM cart and the 16 MB board. It ran the real disc images, the game's
  own variables were read from 32X memory each frame, and every case below was run before a disc
  was burned.

### The fast-load advantage (16 MB cart)
- **No loading screens after the first start** - not for any level, in any world, after any
  power-off. On the 4 MB cart only Icyisland 1-7 stay in the cart; every other level is read from
  the disc the first time it is played (a few seconds with its yellow bar) and is pushed out again
  by the next 5-8 levels played. On the 16 MB cart all 70 stay.
- **Level Select, the bonus islands, going back to an old level:** straight to the level's intro.
- **Later starts cost nothing:** each level is checked against its sum (`SUMS.BIN`) at power-on,
  which takes no visible time. Only a missing or damaged level is copied again - another disc that
  used the cart's page 1, or a power-off during the copy (the copy goes on where it stopped).

### What is seen once
On the **first start with a 16 MB cart**, after the usual loading bar:
**`16 MB CART - COPYING THE LEVELS - ONLY THE FIRST TIME - AFTER THIS NO MORE LOADING SCREENS`**,
`LEVEL n OF 70` and a bar. In the emulator it takes about 3½-4 minutes (each 32 KB piece is one
disc read); on the console it may differ. After that the game starts at the title as usual and the
screen never comes back for that cart. With a 4 MB cart nothing is copied and the game plays as
REV10b did.

### The boot stack
- **REV11-T1 / T2: nothing added.** The motion fix is game code; `tools/check_boot.py` passes.
- **REV11-16MB-T1: command 68** (cart page, window 1), compiled into the Sub-CPU only when
  `CART_MB=8|16` (`STX_CART_PAGED`, passed through `HWFLAGS` - the first two test discs passed it
  through `CCFLAGS`, so 68 was missing and the SH2 waited about 2 minutes at start-up for an answer;
  `build.sh` now stops if `main.c` was compiled without it). It uses the existing 68000 routine 26
  `safe_cart_xfer`; no new 68000 routine. Recorded in the docs repo's
  `CD32X_COMMAND_LEDGER_ADDITIONS.md` (section 7). `tools/check_boot.py` accepts exactly that block.

Still to see on the console: the real board's page switch and how long the first copy takes.
Details: `docs/REV11_T1_NOTES.md`, `docs/REV11_16MB_T1_NOTES.md`, `docs/CART_TEST_BUILDS.md`.

## Why the Sonic CD method ("rip and tear") is faster

**Where the time went before.** In R11 the 32X drew everything: every 16 × 16 cell of the level,
the background, Tux and the bad guys, into a frame buffer, with two SH2s. Scrolling meant drawing
the new cells coming into view, and every 48-64 pixels moving or redrawing the whole picture (the
hitch). The SH2s' time went into copying level pixels into the frame buffer - work that is the
same every frame and has nothing to do with the game.

**What the method does.** The Genesis has a video chip built to show tile maps: two planes of
8 × 8 tiles, each scrolled by writing a register, drawn by the hardware on every line for free.
Sonic CD (and every Genesis platformer) uses it that way. REV puts SuperTux's level on those
planes and leaves the 32X only what really changes each frame:

| | R11 (32X draws all) | REV7 + (Genesis tiles) |
|---|---|---|
| level tiles | drawn by the SH2s into the frame buffer | in Genesis video RAM once per level; shown by the Genesis chip |
| scrolling | redraw new cells; move or redraw the picture every 48-64 px | two scroll registers a frame |
| new tiles coming into view | drawn cell by cell | one map column (30 words) per 8 pixels scrolled |
| the 32X's work | the whole 320 × 176 picture | the moving things (Tux, bad guys, coins in flight, snow) and the sky behind them |
| the hitch | every 48-64 pixels | gone - nothing is ever moved |

The two pictures are combined by the hardware: the 32X sits in front (priority bit), and its
sky colours carry the "through" bit, so they appear behind the Genesis tiles and show wherever a
tile is see-through (REV7-REV9).

**Why it works for SuperTux - its tiles line up with the Genesis grid.** The Genesis can only
show 8 × 8 tiles placed on an 8-pixel grid. SuperTux's tiles are 32 × 32, drawn here at half
size: **16 × 16 - exactly 2 × 2 Genesis tiles**, always on the grid. So:
- each level tile becomes 4 Genesis tiles, made once when the disc is built (`tools/stx_genesis.py`);
- the same tile used 500 times in a level is stored once - a level needs 119-232 Genesis tiles
  plus its animation frames, well inside video RAM (64 KB);
- the level's map becomes the Genesis map directly: scrolling is the chip's job, not ours.

**Games whose tiles do not divide into 8 × 8 (or 16 × 16) cells see little advantage, and why.**
The method only pays when the game's picture is a grid of repeating tiles that falls exactly on
the Genesis' 8-pixel grid.
- *Tile sizes that are not a multiple of 8* (12, 20, 30 pixels ...) or tiles placed at any pixel:
  a tile then lands across Genesis tiles in many different ways, and each way is a different set
  of 8 × 8 pictures. The number of unique Genesis tiles explodes past what 64 KB of video RAM
  holds (about 1,500-2,000 tiles), so most of the picture would still have to be drawn by the 32X.
- *Pictures that are not tiles at all* - large painted backgrounds, scaled or rotated graphics,
  3D (Doom, Virtua Racing): almost every 8 × 8 piece is unique, so there is nothing to reuse.
- *Many colours in one tile*: a Genesis tile has 15 colours from one of 4 palettes (512 colours in
  all, 3 bits a channel). Tiles drawn with hundreds of colours lose detail; the 32X (256 of 32,768)
  keeps them.
In those cases the 32X keeps doing the drawing, and the Genesis can only help with flat
backgrounds or borders.

**What it costs SuperTux.** The level's tiles are in Genesis colours (4 palettes of 15 - REV9),
slightly coarser than the 32X's; animated tiles (coins, water) are uploaded frame by frame;
and the background pictures became 32X gradient skies in the pictures' own colours (REV8-REV9).
In return the levels scroll smoothly with no hitch, and the SH2s are free for the game.

## The rip and tear method in detail

### 1. When the disc is built (`tools/`)
- `stx_convert.py` reads SuperTux 0.1.3's data: tiles (half size, 16 × 16), sprites, levels.
  REV10: tiles whose picture is bigger than one cell (the exit igloo, a tree, statues) are cut
  into pieces over the cells they cover.
- `stx_pack.py` makes each level's pack:
  - `LEVELG.BIN` / `COMPG.BIN` - the level's rules and its pictures as the 32X needs them for
    the things drawn over Tux (foreground tiles) - with the background left out (see-through).
  - `GENLVL.BIN` (`stx_genesis.py genesis_level`) - the level as Genesis data:
    - its pictures cut into **8 × 8 tiles, 4 bits a pixel**; identical and mirrored tiles kept
      once (a level needs 119-232, plus its animation frames; room for 832);
    - **4 palettes of 15 colours** (512-colour Genesis), each tile in the palette that shows it
      best (k-means grouping of the tiles' colours);
    - the level map as **columns** - 2 Genesis columns per level cell, 30 entries each,
      identical columns once - so scrolling sideways needs one column per 8 pixels;
    - **animations** (coins, water, bonus blocks) as groups of tiles with the same speed and
      length: fixed tiles in video RAM, each frame's pictures uploaded over them;
    - plane B: empty (REV8 - the 32X draws the sky).
  - `SKY.BIN` and colours 192-255 - the level's **32X sky**: a gradient (SuperTux's own two
    colours, or the background picture's own colours top / middle / bottom) for each of the
    view's 176 lines, as a 4 × 4 ordered-dither pattern of two neighbouring 15-bit colours (no
    bands). Each colour has the 32X "through" bit.

### 2. The boot path: new commands, nothing changed
The SH2s cannot reach the Genesis video chip; the 68000 can. REV2 added SH2 → Sub-CPU commands
that the Sub-CPU passes to the 68000's existing VDP routines (plus two new small ones). Data goes
from the RAM cart through the 32X's bank window at 0x900000, so the SH2 is never locked out.

| Command | What it does |
|---|---|
| 62 | one word into Genesis video RAM (scroll table, a changed map cell) |
| 63 | tiles / map from the cart into video RAM (up to 1023 words) |
| 64 | colours from the cart into Genesis CRAM |
| 65 | a VDP register (plane addresses, display on / off) |
| 66 | vertical scroll (VSRAM) |
| 67 | map **columns** from the cart (VDP auto-increment 128: down a 64-cell plane) |

Full list and the rules: [CD32X_COMMAND_LEDGER_ADDITIONS.md](https://github.com/MicroNut99/docs).
`tools/check_boot.py` lets exactly these added blocks through and stops the build on any
other change to the boot files.

### 3. Genesis video RAM while a level is on

| Address | Holds |
|---|---|
| 0x0000 | font (tile 0 blank) - kept from the boot |
| 0x4800-0x9FFF, 0xB000-0xBFFF | the level's tiles (832) |
| 0xA000 | plane B map (empty) |
| 0xC000 | the boot text's plane (untouched) |
| 0xD000 / 0xD400 | sprite table (empty) / horizontal scroll |
| 0xE000 | plane A map: the level, 64 × 32 cells, a ring of 64 columns |

### 4. Each level, each frame (`game/gen_level.c`)
- **Level start:** Genesis display off; VDP registers for the layout above; palettes, tiles,
  the 64 map columns around the view, the animation frames, the scroll; display on - and the
  32X switched in front of the Genesis (VDP bitmap mode PRI = 1, REV7).
- **The 32X frame:** the 32X frame buffer is a fixed 320 × 176 layer (no scrolling of its
  own). Only the moving things are drawn - far snow and clouds (with the level's tiles drawn
  again over them), bad guys, Tux, foreground tiles over Tux, near snow, scores. Every
  rectangle drawn is noted (widened to the 16-pixel cells) and restored to the sky the next
  time that buffer is drawn. Nothing else on the 32X changes.
- **Right after each flip** (the frame now shown): the Genesis scroll registers are set to that
  frame's camera, so both pictures change at the same refresh; then new map columns coming into
  view (at most 6 a frame), map cells that changed (a coin taken, a brick broken: 4 words), and
  animation frames that changed.
- **Leaving the level** (world map, menus): Genesis display off, its boot registers back,
  priority back to the Genesis (PRI 0) - those screens are the 32X's as before.

### 5. How the two pictures are combined (the 32X hardware manual's "Priority")
- PRI 1: the 32X picture in front of the Genesis.
- A 32X colour with the through bit is shown on the other side - behind the Genesis planes.
  The sky colours have it, so the sky is behind the level's tiles and shows wherever a tile
  is see-through; Tux, the bad guys and the panel (normal colours) stay in front.
- The PC model (`game/host.c`) composites by these rules; it reproduced REV6's console fault
  (PRI 0: black screen, Tux as an outline) exactly, so mistakes of this kind show up before a
  disc is burned.

### 6. How it is checked before a disc
- PC build of the game with a model of the Genesis chip (`host/`): every level, 900 frames of
  random play, against a reference that clears the whole 32X layer and writes every column
  every frame - must be identical; every level 3000 frames under address / undefined-behaviour
  checkers; the title with its demo.
- The SH2 build; `tools/check_boot.py`; then Fusion (boot), then the console.

## REV rounds
| Round | What was ripped out and what replaced it |
|---|---|
| REV10 | **The exit igloo and the other big pictures** (`docs/REV10_NOTES.md`): SuperTux draws some tiles far bigger than a cell (the exit igloo, a tree, Nolok's statues); since R1 only their top-left corner showed. They are now cut into pieces over the cells they cover. Slower credits; the title's demo uses small Tux again, as SuperTux does. REV10b: the story crawl slower too (a little slower than the credits). |
| REV9 | **The sky without bands, 4 palettes for the level** (`docs/REV9_NOTES.md`): each sky line mixes the two nearest 15-bit colours in a 4 × 4 ordered pattern, so the gradient has no visible steps; the Genesis palette the old sky used now goes to the level's tiles (4 × 15 colours). |
| REV8 | **The sky is the 32X's** (`docs/REV8_NOTES.md`): the Genesis sky (one 15-colour palette, scaled, dithered) is switched off; each level gets a 64-shade 15-bit gradient sky drawn by the 32X behind the Genesis level, as Sonic32X does - SuperTux's own colours for the gradient levels, the picture's own colours for the others. |
| REV7 | **REV6's black screen fixed** (`docs/REV7_NOTES.md`): the 32X was set to sit *behind* the Genesis (the 32X's priority bit, as the game always had it), so the level planes were covered by black and the sprites hidden behind them. The 32X now goes in front while a level is on the Genesis, as the 32X hardware manual describes. |
| REV6 | **The game is built from Genesis tiles** (`docs/REV6_NOTES.md`): every level and the title's demo are Genesis tiles on the Genesis' planes, scrolled by the Genesis chip, the way Sonic CD does it; the 32X draws only what moves. The 32X-drawn version of the levels and the background picture packs are gone from the disc, and so are the Draw and Background options - there is no switch. |
| REV5 | **Fixes for REV4 on the console** (`docs/REV5_NOTES.md`): *Draw: Genesis* no longer needs the background picture's own pack in the cart - every level now carries its Genesis background inside its own data, so the level can't fall back to the 32X when the picture's pack is not there. The timing line shows **DG** (the Genesis is drawing this level) or **DX** (Draw: Genesis is on but this level was drawn by the 32X). The options menu says a Draw / Background change applies from the next level. **Saves fixed:** they now go to the RAM cart's save area (piece 13, the way OpenJazz and OpenLara save), not the Sega CD's backup RAM, which never worked. No boot stack changes. |
| REV4 | **The rip and tear: the level on the Genesis** (`docs/REV4_NOTES.md`): *Options → Draw: Genesis* puts the whole level on the Genesis' plane A and its background on plane B; the Genesis chip scrolls both, the way Sonic CD does. The 32X draws only what moves - Tux, the bad guys, bullets, coins in flight, snow, clouds, scores - and clears just those spots again. No canvas move, no tile redraws, no hitch. The level's colours are the Genesis' (3 palettes of 15 for the level, 1 for the background); the sprites keep the 32X's 256. One more command for the boot stack: 67, map columns into video RAM. |
| REV3 | **The first level picture on the Genesis** (`docs/REV3_NOTES.md`): *Options → Background: Genesis* puts a level's background picture on the Genesis' plane B, where the Genesis chip scrolls it at the level's own background speed (SuperTux's parallax) for free. The 32X draws the level on top with see-through colours where the picture shows. The sideways shift redraws far fewer cells: on the PC, level 1's worst frame drops from 210 cells to 69 when the shift redraws instead of moving the picture. Uses REV2's commands for the first time. |
| REV2 | **The Genesis picture commands** (`docs/REV2_NOTES.md`): the SH2 can now upload tiles and maps from the RAM cart into the Genesis video RAM (63), set single words (62), load Genesis colours (64), set video registers (65) and the vertical scroll (66) - through the 68000 listener's existing VDP routines plus one new one (vertical scroll RAM). Boot diagnostics on (green and white text). **This disc is a boot test for Fusion:** it must start exactly as before. |
| REV1 | **The tiler's copying.** New rows of cells (a row coming in at the top or bottom, the redraw after a sideways shift, a new level) were copied cell by cell by the CPU; they can now be put together a line at a time in fast RAM and copied into the frame buffer by the master SH2's **DMA unit** while the next line is put together (*Options → Draw: DMA*; Draw: CPU is R11's way). **Timing: Worst** shows the slowest frame of the last 64 and what it spent its time on, including the sideways shift – the hitch the next rounds go after. |

REV1 was a step on the SH2 side only (faster copying, a better timing line) - not the rebuild.
REV2 starts the rebuild: the commands the Genesis planes need, tested for booting first.

---

SuperTux 0.1.3 ("Milestone 1", 2004 – the classic platformer with Tux) on the **Sega CD32X**,
drawn with Victor Luchits' **YATSSD** tile renderer on the boot, cart and Sub-CPU layers of the
Hurrican CD32X port. **Version REV10b (from R11).** The whole game runs from one CD: the title with its demo
level, Icyisland and both Bonus Islands with their world maps, all 76 levels, every bad guy and
power-up, CD music, PCM sound effects, saving and high scores. The levels are kept in a 4 MB RAM
cart once they have been read from the disc.

> **SuperTux is free software (GPL-2.0).** The build downloads SuperTux 0.1.3 itself and
> converts its data; nothing of SuperTux is stored in this repository. A disc built from it
> falls under GPL-2.0 too.

**This is an exercise, not an improvement.** The point is to practise building a Sega 32X / CD32X
game from source code and data that are already available. SuperTux gains nothing from running
on the 32X: it looks and plays no better than on a PC, and the hardware forces small losses
(half resolution, 256 colours, keyed edges instead of soft ones, no floating point).

## Hardware
- Sega Genesis / Mega Drive
- Sega CD / Mega-CD
- Sega 32X
- **A 4MB RAM cart** (battery-backed), in the 32X cartridge slot – required
- a 3- or 6-button pad

Emulators: **Kega Fusion** is the only emulator that boots a CD32X disc, and it cannot emulate
the 4MB RAM cart: the game stops at the splash there. ares does not emulate the CD32X boot
(it cannot start Night Trap CD32X either). REV is tested on the PC (`host/`: the pictures) and
on real hardware (the timing).

## What you need
| What | Where it goes |
|---|---|
| SuperTux 0.1.3 | fetched by the build (`tools/stx_fetch.sh`, tag v0.1.3 of github.com/SuperTux/supertux) into `SuperTux-0.1.3/` |
| Chilly Willy's Sega toolchain (`/opt/toolchains/sega`: sh-elf for the 32X, m68k-elf for the Sega CD) | your build machine (Linux / WSL) |
| `git`, `gcc`, Python 3 with numpy and Pillow | your build machine |

The music is SuperTux's own (its modules rendered to CD audio tracks at build time with
libxmp-lite), and so are the sound effects, the pictures and the splash.

## Building
```bash
./build.sh 2>&1 | tee build_log.txt
```
1. The first run fetches SuperTux 0.1.3.
2. The build converts the data, makes the splash, the sound effects and the CD tracks, checks
   the game on the PC (`host/`), builds the SH2 program, the loader and the Sub-CPU side.
3. **REV10b: `cd32x/subcpu/SuperTuxREV10b.cue`** - no boot stack changes since REV4 (which booted), so run it in Fusion as usual, then burn it (with its `.bin`). It has the data track and the 12
   music tracks.

`DATA_ONLY=1 ./build.sh` stops after the data and the PC check (no Sega toolchain needed).
The build stops at once if a boot path file differs from Hurrican's (`tools/check_boot.py`).

## Starting the game
Put the RAM cart in the 32X, the disc in the Sega CD, and switch on. The SuperTux picture appears
with a loading bar while the program and the game's data are copied into the cart; the title
follows with Tux running through its demo level behind the menu.

## Fast start and the cart
The RAM cart keeps its contents without power, and the game uses it as a store of levels.

- **The data at power-on:** if the cart already holds the same data from the last time, the boot
  only checks it – the bar jumps to full and nothing is copied. After a new build, the first
  start copies it again.
- **The title never waits:** its demo level is part of that data.
- **A level is read from the disc once.** Each level has its own place in the cart. Icyisland's
  levels 1–4 stay there until the console is switched off; the others stay until newer levels
  need their room (about the last 5–8 played). Dying, restarting, aborting, coming back to a
  level, the world map and the title: no disc reads.
- **A level read** shows its name, the lives, its author and a yellow bar. Only the level itself
  is read (R2 also read the splash picture twice before every level).

**16 MB cart (REV11-16MB-T1):** every level has its own place in pages 1–7 and all of them are
copied in at the first start (once), so no level is ever read from the disc again – see
"REV11 test builds" above.

Cart layout: program 0–1 MB, shared data and the title's level 1–1.6 MB, world 1's first levels
1.6–2.5 MB, the other levels 2.5–3.94 MB; the last 64 KB belong to the platform layer. The game's
saves and high scores are in the Sega CD's backup RAM. The layout is
explained in `tools/stx_pack.py`.

## Controls
| Button | Action |
|---|---|
| Left / Right | walk |
| **B** (held) | run, carry Mr. Iceblock; fire Tux shoots |
| **C** or **A** | jump (hold it to jump higher) |
| Down | duck (big Tux) |
| **Start** | pause menu |

**World map:** the pad walks Tux along the paths, **A** or **C** enters a level or a teleporter,
**Start** opens its menu (Continue, Options, Quit Game).

**Menus:** up/down chooses, **A**, **C** or **Start** selects, **B** goes back; in Options
left/right changes a setting. *Options → Jump* swaps A and B (C/B jump, A runs).

## What is in this version
- **The title:** SuperTux's demo level with Tux walking and jumping by himself behind the logo;
  Start Game, Bonus Levels, Options, Credits, High Scores.
- **Five save slots** (a new game shows the story first), plus one save for each Bonus Island.
- **Icyisland** (26 levels) and **Bonus Island I and II** (50 levels) with their world maps:
  paths, one-way paths, teleporters, messages, red and green level dots, Tux walking on by
  himself after a level, the ending text and the credits after the last level.
- **Level Select** (under Bonus Levels, and in Options from the title): any of the 76 levels on its own.
- **Tux** small, big and fire, running, skidding, ducking, ice, bonus boxes (coins, eggs, ice
  flowers, herrings, 1-ups), bricks, coins, invincibility, reset points, the time limit with
  hurry-up music, the end-of-level walk.
- **Every bad guy:** Mr. Iceblock (kick him, carry him), Mr. Bomb and his bomb, Jumpy, the
  stalactite, the flame, the fish, Spiky, the snowball, the bouncing and flying snowballs.
- Animated tiles, snow and clouds, floating scores, broken bricks.
- **Pause menu:** Continue, Restart Level, Options, Abort Level, Main Menu.
- **Options:** music, lives for a new game, the timing line (off / on / worst), the jump button,
  motion (smooth / steps), scroll (ring / classic, the world map's), level select (from the title), controls, the story, high scores, credits (with a
  music player: left/right plays SuperTux's songs).
- CD music (SuperTux's 12 tracks), 20 sound effects on the Sega CD's PCM chip, high scores and
  saves in the RAM cart's save area (REV5).

## Known limitations
- **Half size:** SuperTux's 640 × 480 is drawn at 320 × 240 (tiles and sprites sharpened, the
  backgrounds not); the 32X shows the
  whole width and 352 of the 480 lines (176 pixels), with the camera following Tux up and down.
  The status line is in a panel under the level.
- REV6: the levels are Genesis tiles - the Genesis' colours (REV9: 4 palettes of 15). REV8: the sky is
  a 32X gradient fixed to the screen (SuperTux's background pictures are not shown).
- 24 snowflakes instead of SuperTux's 128; the far flakes are not drawn over tiles.
- No level editor and no Quit in the main menu.
- Levels cannot be read while music plays (the CD drive can't do both), so the music stops
  during a level read.

## Frame rate
### REV1: reading the timing line
*Options → Timing* goes **Off → On → Worst** (left / right).
- **On** (R4–R11): the average of 16 frames – `F` frames a second, then milliseconds a frame for
  `L` the rules, `T` the tiles, `S` the sprites, `H` the panel, `W` waiting for the refresh, `N`
  tiles drawn a frame.
- **Worst** (REV1): the slowest frame of the last 64 (about a second), held on screen:
  `X` its time before it could be shown (L + T + S + H; over 16.7 ms = a refresh was missed),
  its `L T S H`, `M` what the sideways / up-down shift did in that frame (`M-` none, `M3.1` the
  picture was moved and that took 3.1 ms, `ML` it was left in place and the cells that differ
  were drawn – they are in `T` and `N`), `N` its cells and `D` how many of them went through
  the DMA rows.
- With the timing on, the second panel line ends in `DG` in a level (REV6: the Genesis draws
  every level).

**R11 (before the rebuild): 38–56 frames per second** in Icyisland's first level, measured on real
hardware with the timing line (*Options → Timing*). R2 ran at 17–22. **REV7-REV10b (Genesis
tiles) have not been measured yet** - see "Release: REV10b" above.

The 32X shows a new picture only at a screen refresh (60 a second), so 60 is the ceiling: F on
the timing line counts real buffer swaps against the refresh counter. A frame now takes about
10 ms of work, inside one 16.7 ms refresh – most frames wait for the refresh (W).

| Part of a frame | R2 | R3 |
|---|---|---|
| F – frames a second | 17–22 | **38–56** |
| L – the game's rules | 6.5–9.5 ms | 1.9–2.8 ms |
| T – drawing the tiles (both SH2s) | 22–29 ms | 3–9 ms |
| N – tiles drawn a frame | 140–245 | 9–100 (mostly 13–35) |
| S – drawing the sprites | 1–2 ms | 1–1.5 ms |
| H – the status panel | 7–9 ms | 1–4 ms |
| W – waiting for the screen refresh | 0.5–6 ms | 6–9 ms |

### R10
- **Scroll: RING** (Options, on by default): the 32X's own line table wraps the picture round
  vertically, so the view going up or down only draws the row coming in - the shifts every 16
  lines (the catches while jumping) are gone. CLASSIC is the old way, to compare.
- Sideways shifts: when redrawing is cheaper only the tiles that differ are drawn, and the shift
  is done early, on a frame with time to spare.
- Fixed: a 16-line strip of old picture when the view moved sideways and up at once (R4–R9).

### R7
- **Jumping:** the view re-anchors every 16 lines up or down; that move was done by one SH2.
  Now both share it, and the game times moving against redrawing as it plays and uses the
  cheaper one.

### R6
- **Smooth motion.** SuperTux moves the world in 10 ms steps; the screen shows a picture every
  16.7 ms, so pictures were 2, 2, 1, 2, 2, 1 steps apart and the view and Tux jerked even at
  60 frames a second. Each picture now draws everything between the last two steps, where it
  is at the clock's time (the rules are unchanged; only where things are drawn).

### R5
Measured on the PC (cells the tiler draws a frame, Tux running): snowy levels 41–50 → 24–30,
Above the Arctic Skies (clouds) 80 → 48, Welcome to Antarctica 14 → 13. Not yet measured on the
console.
- **Sprites cut to their visible pixels:** every cell a sprite touches is drawn again later, and
  SuperTux's snowflakes were mostly empty 32 × 32 pictures.
- **No lock per cell:** one SH2 lists the cells that changed, then both draw half of them (R3
  had both SH2s take turns through a locked counter for all ~260 cells of the view, every frame).
- **Kept clouds:** a cloud is drawn again only when it moves or something passes in front of
  it, and then only where needed.
- **Animated tiles:** only the cells of the animations that changed frame are drawn again.
- **Far snow** (behind the tiles) is skipped where it would be hidden by a tile anyway.

### R4
- **The 64-pixel moments:** when the view crosses 64 pixels sideways (or 16 lines up/down) the
  canvas's pixels are moved by both SH2s with 32-bit copies and only the new strip of cells is
  drawn, instead of the whole screen. Expect the 38–42 dips to shrink; not yet measured on the
  console.
- **Background: Moves** draws the background picture every frame and the level's tiles over it as
  sprites, like SuperTux. It is slower (expect roughly R2–R3 speeds, more on busy screens) and
  is off by default. It takes effect from the next level.

### What was done (R3)
- **Scrolling costs almost nothing.** Vic's renderer draws into a canvas a little larger than
  the screen; scrolling only changes where the video chip starts reading lines, plus the
  32X's 1-pixel shift. Only the tiles coming into view are drawn.
- **One picture a cell.** A SuperTux level has four layers (the background picture, the
  background tiles, the tiles Tux stands on, the foreground). Vic's renderer remembers one tile
  number for each cell of its canvas and skips cells that have not changed – but with four
  layers almost every cell was redrawn every frame. The build now merges each cell's layers
  into one 16 × 16 picture, so a cell is drawn only when it scrolls in, changes (a coin is
  taken, a box emptied, an animation frame) or had a sprite over it. Tux, the bad guys and the
  clouds get the right tiles drawn over them again where they should be behind.
- **Both SH2s draw the tiles.** Vic's renderer hands out the cells through a shared counter;
  the second SH2 runs the same drawing call at the same time.
- **Exact sprite marks.** Vic's code marked whole columns of cells as changed under every
  sprite; ours marks only the cells the sprite covers.
- **The status panel** is drawn only when its text changes.
- **The busy code runs from the 32X's fast RAM**, not from the cart (the rules, the level,
  Tux, the bad guys, the drawing calls).
- **What was left (fixed in R4):** every 64 pixels sideways the canvas was re-anchored and the
  screen drawn whole once (the 38–42 fps moments, N 76–103).

## Versions
| Version | What changed |
|---|---|
| R1b | The first disc: world 1, Tux walking, running and jumping, coins, the HUD. |
| R1c–R1f | The SuperTux splash with a loading bar, louder music, the menus and extras of the other CD32X ports, options in the pause menu. |
| R2 | The whole game: title with demo, slots, world maps, all levels, every bad guy and power-up, texts, high scores. Half size, sharpened. 17–22 fps. |
| R3 | 38–56 fps: one merged picture a cell, both SH2s drawing, exact sprite marks, a cheap panel, the busy code in fast RAM. The cart as a store of levels: the title's level from power-on, levels read once. |
| R4 | The 64-pixel re-anchor moves the canvas's pixels instead of redrawing the screen. *Options → Background: Moves* gives SuperTux's slower-scrolling backgrounds (off by default; their pictures are read into the cart with the level). Small sprites at the screen edge are drawn instead of dropped. |
| R5 | The backgrounds are no longer sharpened. Fewer cells drawn a frame: sprites cut to their visible pixels, the cells listed and shared between the two SH2s without a lock per cell, clouds kept in the picture, animated tiles redrawn only when theirs changed, far snow not drawn over tiles. Snowy levels draw about 40 % fewer cells, the cloud level 40 % fewer. |
| R6 | Smooth motion: the world's 10 ms steps against the 60 Hz screen made pictures 2, 2, 1 steps apart (jerky even at 60 fps); everything is now drawn between the last two steps, at the clock's time. |
| R7 | The dips while jumping (F 33-40): the view's up / down re-anchor shared by both SH2s, and the console measures whether moving the picture or drawing it again is cheaper and does that. |
| R8 | Jumping with A: a single pad read without the button no longer cuts the jump short. The timing line shows the raw pad. |
| R9 | Finding the stuck buttons: the timing panel counts pad replies that came back old (S); Options → Motion turns R6's smooth drawing off to compare. |
| R10 | Options → Scroll RING: the picture wraps up and down, so the view never has to be shifted when Tux jumps; sideways shifts redraw only what differs and happen early on quiet frames; a strip of old picture (R4–R9) fixed. |
| R11 | Options → Level Select (from the title): any of the 76 levels. |
| **REV11-16MB-T1** | 16 MB cart: every level copied into the cart at the first start, no loading screens after; command 68 (cart page). |
| **REV11-T1 / T2** | Even motion: the view moved 3, 5, 3 px a frame (the demo's jitter); the build's name on the title. |
| **REV10** | The exit igloo, the tree, the statues - big pictures drawn whole; slower credits; small Tux in the title demo. |
| **REV9** | Dithered 32X skies (no bands); 4 Genesis palettes for the level. |
| **REV8** | The 32X's gradient skies behind the Genesis level (Sonic32X's way); the Genesis sky off. |
| **REV7** | The 32X in front of the Genesis while a level is on the Genesis' planes (REV6's black screen). |
| **REV6** | The game built from Genesis tiles: every level and the demo on the Genesis' planes, no 32X version of the levels, no Draw / Background switch. |
| **REV5** | Draw: Genesis keeps the level on the Genesis (its background inside the level's data); DG / DX on the timing line; saves in the RAM cart's save area. |
| **REV4** | Options → Draw: Genesis - the level on the Genesis' planes, the 32X draws only the sprites. Command 67 (columns into VRAM). |
| **REV3** | Options → Background: Genesis - the background picture on the Genesis' plane B, scrolled by the Genesis chip; the level drawn over it by the 32X. |
| **REV2** | The Genesis picture commands 62-66 (Sub-CPU) and the 68000's vertical-scroll write; boot diagnostics on. A boot test for Fusion. |
| **REV1** | The rip and rebuild starts (see the top). Options → Draw: DMA – rows of new cells copied by the SH2's DMA unit while the next line is put together. Options → Timing: Worst – the slowest frame and what its shift cost. |

## Files
| Path | Contents |
|---|---|
| `game/stx.h` | the game's map: what each file does, the shared data and calls |
| `game/res.c`, `level.c` | the shared data (sprites, letters, pictures, texts); a level, the cart store, the tile rules |
| `game/tux.c`, `objects.c` | Tux (player.cpp); bad guys, upgrades, bullets, bricks, scores (badguy.cpp, special.cpp, gameobjs.cpp) |
| `game/game.c` | a level being played (gameloop.cpp, world.cpp): camera, particles, music, the end, drawing |
| `game/wmap.c`, `menus.c` | the world maps (worldmap.cpp); title, menus, texts, high scores, saves, `main()` |
| `game/yt.h`, `yt_layers.c`, `yt_mars.c` | Vic's renderer on Hurrican's screen; the level reader |
| `game/host.c`, `yt_host.c`, `host/` | the PC build that checks the game: the platform layer and Vic's drawing as C stand-ins |
| `mars/Makefile` | the SH2 program |
| `yatssd/` | Victor Luchits' YATSSD files, unchanged (`YATSSD_COMMIT.txt`: the version) |
| `cd32x/`, `port/` | the Hurrican CD32X port's boot path and platform layer, unchanged |
| `tools/stx_convert.py`, `stx_pack.py` | SuperTux 0.1.3 → the 32X's data; the disc's files and the cart layout |
| `tools/make_stx_sfx.py`, `make_stx_splash.py`, `stx_music.sh` | sound effects, the splash, the CD tracks |
| `tools/check_boot.py`, `check_sdram_code.py`, `where.sh` | build checks; naming the addresses on a red exception screen |
| `docs/` | developer notes: `REV10_NOTES.md`, `REV9_NOTES.md`, `REV8_NOTES.md`, `REV7_NOTES.md`, `REV6_NOTES.md`, `REV5_NOTES.md`, `REV4_NOTES.md`, `REV3_NOTES.md`, `REV2_NOTES.md`, `REV1_NOTES.md`, `STX_HANDOFF.md`, `R11_NOTES.md`, `R10_NOTES.md`, `R9_NOTES.md`, `R8_NOTES.md`, `R7_NOTES.md`, `R6_NOTES.md`, `R5_NOTES.md`, `R4_NOTES.md`, `R3_NOTES.md`, `R2_NOTES.md`, `R1B_NOTES.md`, `BOOT_PATH_NOTES.md` |

## Credits
- **SuperTux** – Bill Kendrick (original author), Tobias Glaesser (project leader), Ricardo Cruz
  and the SuperTux team: https://github.com/SuperTux/supertux (GPL-2.0, code and data). The
  music modules are by their own authors (see SuperTux's credits).
- **YATSSD** – Victor Luchits (viciious): the tile renderer (MIT, `yatssd/LICENSE_YATSSD.txt`)
- **Hurrican CD32X / OpenJazz CD32X ports** – the boot path, cart and Sub-CPU layers; the CD32X
  loader from the **OpenLara CD32X** port
- **Sega CD / 32X boot and command framework** – from the Kobo Deluxe CD32X project, built on
  Chilly Willy's Sega CD and 32X examples and toolchain
- **4MB 32X RAM cart** – Victor Luchits, Joseph Fenton, Leo Oliveira, Tiido Priimägi and Eric Witt
- **libxmp-lite** (LGPL 2.1) renders the music at build time; nothing of it is on the console
- SuperTux port – MicroNut99

Licences: GPL-2.0 for SuperTux and a disc built from it; see `yatssd/` and each upstream
project's own licence.
