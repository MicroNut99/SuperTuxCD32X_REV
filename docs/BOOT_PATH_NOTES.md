# The CD32X boot path in this repository - annotated

Notes for the next person or AI working on SuperTux CD32X. They explain what every part of the
boot path does, where (file and line), and which rules keep it working. **The boot files
themselves are not annotated in place**: their bytes must stay as Hurrican CD32X's (commit
37852a6), and `tools/check_boot.py` stops the build on any change. So the annotations live here.

Sources, marked in the text:
- **[code]** read in this repository's files (line numbers as of commit d4a6fc8);
- **[Kobo doc]** `KOBO-CD32X_BOOT_PROCESS.pdf` and `KOBO-Sega 32XCD Boot Sequence and Sub-CPU
  Command Ledger.pdf` (in github.com/MicroNut99/OpenJazzCD32X, `docs/`) - written for the
  Kobo project, the ancestor of this boot path; where Hurrican's files differ, the code wins;
- **[OpenLara]** `cd32x/ref/openlara/OPENLARA_CD32X_HANDOFF3.md`;
- **[Hurrican]** github.com/MicroNut99/HurricanCD32X, `docs/HURRICAN_CD32X_HANDOFF.md`.

## 0. The one rule

**Never edit the boot path.** Hurrican's first benchmark disc changed the platform layer (no C++
start-up, a different present function, no sound pack, its own module name) and gave a black
screen on the console from the start; the same program on the unchanged boot path booted
[Hurrican]. Everything new runs from `main()` and uses the platform layer's existing calls.
The only deliberate differences here are the diagnostics switches (section 7), each checked.

What counts as the boot path (all listed in `docs/BOOT_FILES.md5`):
- `cd32x/subcpu/` - Sub-CPU program and the Genesis 68000 listener, the disc build;
- `cd32x/sh2/` - the SH2 loader that copies the game into the RAM cart and starts it;
- `cd32x/ref/` - OpenLara's originals, kept for reference (`prep_openlara_cd.py` is used by the build);
- `port/` - the 32X platform layer (start-up, screen, pad, packs, sound, CD commands);
- `tools/make_oj_rom.py`, `tools/make_boot_image.py`, `tools/mars_boot.bin` - the cartridge
  image of the game program and the splash picture.

## 1. The processors [Kobo doc]

| Processor | Program here | Role |
|---|---|---|
| Sega CD Sub-CPU (68000, 12.5 MHz) | `APP.BIN` = `cd32x/subcpu/` (crt0.s, main.c, scd.c, cd.s ...) | the coordinator: reads the CD, CD audio, PCM chip, writes the cart, answers the SH2 |
| Genesis 68000 | the listener in `cd32x/subcpu/hw_md.s` (`md_init_start` .. `md_init_end`), run from Genesis RAM 0xFF1000 | command slave of the Sub-CPU: Genesis VDP and text, pads, 32X registers and frame buffer, the cart window |
| 32X master SH2 | first the loader (`cd32x/sh2/`), then the game (`port/` + `game/`) | the game |
| 32X slave SH2 | started by the loader | the game's helper (sound mixing / drawing jobs) |

Every request from the SH2 travels SH2 -> 32X COMM registers -> Sub-CPU -> Sega CD comm
registers -> Genesis 68000 -> hardware, and the reply comes back the same way. The SH2 cannot
reach the CD; the Sub-CPU cannot reach the 32X; the 68000 can reach both but not the CD.

## 2. Boot, step by step

1. **BIOS** loads the disc's boot block (`US_BOOT.BIN` from the toolchain, written by
   `genisoimage -generic-boot`, `cd32x/subcpu/Makefile` line 105) [Kobo doc].
2. The boot block loads **`APP.BIN`** into Sub-CPU program RAM and calls `_start`
   (`cd32x/subcpu/crt0.s`) [Kobo doc]: interrupts off, BSS cleared, own stack,
   `init_hardware` (copies the Genesis code `md_init_start`..`md_init_end` to Word RAM
   0x080000 = 0x200000 for the Genesis), C initialisers, `main()`.
3. **Sub-CPU `main()`** (`cd32x/subcpu/main.c` line 410) hands Word RAM to the Genesis (first
   two lines). `KOBO_BOOT_TEXT` 1: the Genesis display is not switched off here (line 414).
4. **Genesis 68000** runs `md_init_start`, copies the listener to 0xFF1000 and jumps there;
   `md_init_hw` sets up the VDP, Chilly's font, the three-line palette (line 0 white 0x0CCC,
   line 1 green **0x00A0** - `hw_md.s` line 435, line 2 red) and the V-blank handler, and
   (here) leaves the display **on** (`0x8174`, line 444 - see section 7). Then the listener
   loop waits for Sub-CPU commands [Kobo doc + code].
5. **Waking the 32X** (`hw_md.s` `init_32x`, Sub-CPU command `MD_CMD_INIT_32X` = 18): checks
   "MARS" at 0xA130EC, sets ADEN, clears COMM0-6, clears both frame buffers and the palette,
   checks for "SDER", copies the SH2 program (the **loader**, `ROMS/CDLOADER.BIN` built into
   `APP.BIN` by `files.s`) into the frame buffer, writes **"_CD_"** to COMM0 and waits for
   "M_OK" / "S_OK". Chilly's **white lines** ("Activated 32X", "Cleared 32X", "32X Waiting",
   "CD BOOT TABLE" ...) are printed here [Kobo doc]. Error results: -2 no 32X, -3 SDRAM
   self-test error, -5 adapter not ready.
6. **32X boot ROMs** read the loader's CD boot table (`cd32x/sh2/cdl_crt0.s`, 56 bytes - the
   loader has its OWN start-up and linker script; D32XR's cartridge crt0 never reached main(),
   `cdloader_sh2.c` lines 3-7), copy it to SDRAM and start it [code].
7. **Start handshake** (COMM10): the loader writes 0x0AAA and waits for 0x0BBB from the Sub-CPU
   (`cdloader_sh2.c` lines 295-296). The game program does NOT repeat it
   (`port/plat_mars.c` header, line 9).
8. **The loader** (`cdloader_sh2.c` `main()`, line 284) [code]:
   - Genesis text on/off by `OJ_DIAG_TEXT` (CMD 52, line 306); status **4D01**;
   - `splash_show()` (line 251): `IMAGE.RAW` (file 0) into the frame buffer with CMD 51 - the
     splash picture and its empty bar; with `OJ_DIAG_TEXT` 1 the text stays IN FRONT
     (DISPMODE 0x81, line 271);
   - opens **file 20 = `OPENLARA.32X`** (the game program, CMD 38, line 315);
   - **quick check** 4D06: compares the first, middle and last 32 KB chunk with the cart (CMD
     47); all equal -> **4D07**, no copy (hold **A+B+C** at power-on to force the copy);
   - otherwise CMD 46 per chunk into the cart at 0, progress **4C00..4C3F**, the bar grows;
     a bad chunk -> **4DE2**;
   - **4D02** loaded; checks "SEGA" at 0x100 and the 32X header at 0x3D4..0x3EC (sizes, entries
     in SDRAM 0x06xxxxxx), else **4DE3**; **4D03**;
   - copies the hand-over routine to `TRAMP_ADDR` 0x2603C000 (top of SDRAM), parks the slave
     there (**4D04**, or **4DE4** if it does not park), **4D05**;
   - jumps into the routine: it copies the game's SDRAM part from the cart over the loader and
     starts both SH2s at the game's entries (OpenLara mode: COMM0 = COMM4 = 0 instead of
     M_OK / S_OK, lines 8-9).
9. **The game program's start-up** (`port/mars/crt0_oj.s`, then `port/plat_mars.c`
   `plat_mars_entry()`, line 460) [code]:
   - takes the frame buffer, starts the watchdog clock, sets the stack guard;
   - 256-colour mode; the Genesis text stays as the loader left it if `OJ32X_DIAG_TEXT` is 1
     (line 474), otherwise off;
   - `OJ32X_SPLASH` 1 (line 71): the loader's splash stays on screen;
   - `cxx_start()` (line 493): C++ global constructors, the unwinder;
   - `pack_load()` (line 404): the **resident pack** = file 8 = `IMAGE8.RAW` into the cart at
     slot 8 (0x100000, `PACK_SLOT` line 103); must start with **"OJPK"** (`port/pack.h`); same
     quick check (first/middle/last chunk, A+B+C forces); the bar fills its second half;
   - `sfx_load()` (line 594): `SFX00.BIN` into the PCM chip (CMD 53);
   - waits `OJ32X_SPLASH_HOLD` (120 frames) since the splash appeared;
   - **switches the Genesis text off** (`set_genesis_text(0)`, line 500) - always;
   - `boot_test()` (line 501): the J4a test screen (each start-up check, the live pad, the
     clock; Start goes on);
   - writes **STATE 4B10** to COMM12 (line 508): the Sub-CPU stops reprinting its rows;
   - calls the game's **`main()`**. From here on: new code, using the platform calls.

## 3. The disc

Built by `cd32x/subcpu/Makefile` target `cd` (lines 98-105) + `make_mixed_cd2.py` (audio
tracks). Files on the data track (`IMAGE_FILES`, line 45): `APP.BIN` (Sub-CPU + 68000 +
loader inside), `OPENLARA.32X` (the game program, file 20), `IMAGE.RAW` (splash, file 0),
`IMAGE8.RAW` (resident pack, file 8), `IMAGE9.RAW` if present, `OJnnn.PAK` (level packs, file
200+n), `SFXnn.BIN` (PCM sound packs), `*.PKD` (OpenLara). Names come from the file number in
`main.c` lines 640-650. Audio: track 1 data, tracks 2.. the music.

The game program carries the module name **"OpenJazz 32X"** and goes on the disc as
`OPENLARA.32X` - Hurrican keeps both (its build checks the name, `build.sh`); so will this port.

## 4. Cart layout (4 MB; the SH2 reads it at 0x02000000 cached / 0x22000000 uncached)

| Offset | Holds | Loaded by |
|---|---|---|
| 0x000000 | the game program (`OPENLARA.32X`) | the loader, at power-on (skipped when already there) |
| 0x100000 | resident pack `IMAGE8.RAW` (`PLAT_CART_DATA_OFFSET`) | `pack_load()`, at start-up (skipped when already there) |
| 0x280000 | level pack `OJnnn.PAK` (`PACK_LEVEL_CART`, max 0x170000) | the game, `plat_load_pack()` |
| 0x3F0000 | settings and saves | the platform layer through the 68000 |

**The SH2 cannot write the cart** (OpenLara lesson 1): every write goes through the 68000
(CMD 46, 61). After the 68000 wrote it, the SH2's cache must be purged (`pack_load` does it).

## 5. SH2 -> Sub-CPU commands used here [OpenLara + code]

COMM0 = command (low byte; for 48 the high byte is a sound effect), COMM2 = parameter; the
Sub-CPU clears COMM0 when done. The SH2 waits with pauses between reads (`comm_pause`,
`send_cmd`, `plat_mars.c` lines 190-196) and with a time limit.

| Cmd | Does |
|---|---|
| 38 | open file (COMM2 = number) -> length in COMM8 |
| 45 / 54 | play CD track (repeat) / stop |
| 46 | 32 KB chunk -> cart (bit 15 of COMM2: no read-back); COMM12 = 0 verified |
| 47 | compare a chunk with the cart (the quick check) |
| 48 | pad -> COMM8 (and the Genesis tick count) |
| 51 | file chunk -> frame buffer (the splash) |
| 52 | Genesis display on / off (COMM2 = 1 / 0) |
| 53 | `SFX<COMM2>.BIN` -> PCM chip; COMM12 = number of effects |
| 58 / 61 | save area (through the 68000 into the cart) |

COMM12 also carries the SH2's **STATE** code, which the Sub-CPU prints in green (row 10).

## 6. Status codes

Loader: 4D01 open, 4D06 quick check, 4D07 already in the cart, 4C00-4C3F chunk n, 4D02
loaded, 4D03 header OK, 4D04 slave parked, 4D05 starting; errors 4DE1 file not found, 4DE2 bad
chunk(s), 4DE3 not a 32X image / bad header, 4DE4 slave did not park.
Game: 4B10 = `main()` reached, the game runs. (The Kobo ledger's 4B01-4B08 / 4BE1-4BE6 are
Kobo's own game codes, not used by this boot path.)

## 7. Diagnostics - what is on here, and where

| Switch | File, line | Release (Hurrican) | Here |
|---|---|---|---|
| `KOBO_DIAG_TEXT` | `cd32x/subcpu/main.c` 93 | 0 | **1** - green text |
| `KOBO_BOOT_TEXT` | `cd32x/subcpu/main.c` 103 | 1 | 1 - display not switched off by main() |
| `SCD_DIAG_TEXT` | `cd32x/subcpu/scd.c` 31 | 0 | **1** - green CKPT / INITCD / CD errors / root directory |
| `OJ_DIAG_TEXT` | `cd32x/sh2/cdloader_sh2.c` 15 | 0 | **1** - text on and in front of the splash |
| display at the end of `md_init_hw` | `cd32x/subcpu/hw_md.s` 444 | 0x8134 off (OL-13) | **0x8174 on** - white lines from the start |
| `OJ32X_DIAG_TEXT` | `port/plat_mars.c` 62 (build flag) | 0 | **1** - text kept until C++ start-up worked |

Why (Micronut99): the first builds must never be a black screen - the white and green lines
show how far a boot got. A release turns them off and the splash does that job: it tells the
player the system is working while a slow boot loads.

**Every green line, and what can still hide it** (checked in the code):
- `print_diag()` (`main.c` 124-133) - gated by `KOBO_DIAG_TEXT` and `quiet`; `quiet` is only
  set by command 44 when `SLIDESHOW_DEBUG` is 0 (line 837) - it is 1, so never.
- rows written by `main.c`: 0 build line, 1-9 CD / Sub-CPU / listener, 10 `STATE:` (only when
  it changes, stops at 4B10 - HW5), 11-17 file and chunk lines, 18 SH2 checks, 19 32X init /
  heartbeat `S.... C0: ST: M: SL:` (every 64 passes, stops at 4B10 - OL-2b), 20-27 errors,
  cart, pad.
- `scd.c` 34-167: CKPT lines (row 16), time-outs (row 17), row 2, the root directory list
  (rows 19+) - gated by `SCD_DIAG_TEXT`.
- the colour: palette line 1 = 0x00A0 (`hw_md.s` 435).
- the platform layer switches all Genesis text off before the game's `main()` (`plat_mars.c`
  500); and STATE 4B10 stops the per-pass rows. Why the rows stop: each print swaps Word RAM
  and goes through the 68000; reprinting every pass dropped Kobo to 1-2 fps (HW5).

## 8. Hardware rules (seen on the console; Fusion shows none of them) [Kobo doc + OpenLara]

1. The 68000 reads the Sub-CPU's command flag **twice** and acts only if both match (torn reads).
2. Never hammer a COMM register the other CPU writes; one writer per register; pauses between polls.
3. **No CD data reads while CD audio plays** - stop the music before loading a pack.
4. Frame buffer: 16-bit writes.
5. Colour 0x0000 is see-through on the 32X; colour 0 is what a cleared screen shows - keep it black.
6. Diagnostic text costs time even when hidden.
7. The SH2 can read the cart but not write it; while the 68000 copies, SH2 cart reads just wait.
8. COMM6: OpenLara's renderer used it for master/slave sync; anything else writing it froze the
   game. (YATSSD's tiler also uses COMM6, as a tile counter for its two-CPU drawing - check the
   platform layer's use before turning that on.)
9. Fusion cannot emulate the RAM cart: the disc is tested on the console.

## 9. What the game adds (from R1b) - outside the boot path

- `game/` - SuperTux's rules in C, called as `main()` by the platform layer;
- `mars/` - Hurrican's engine Makefile (same flags as J19b, plus `-DOJ32X_MUSIC=1` and
  `-DOJ32X_DIAG_TEXT=1` in `EXTRA_DEFS`);
- the packs: `IMAGE8.RAW` (resident, "OJPK"), `OJnnn.PAK` (levels), `SFX00.BIN`, made by
  `tools/` from SuperTux's data;
- YATSSD's tiler fed through the platform's frame-buffer calls (its own hardware set-up,
  `Hw32xInit`, is not used: the platform layer owns the screen).
