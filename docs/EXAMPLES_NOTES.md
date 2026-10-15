# What the five example ports teach SuperTux CD32X

Read in October 2026 before writing R1b's code. Each point names where it was found, so the next
person can check it. Sources are Micronut99's repositories:

- `HurricanCD32X`: `docs/HURRICAN_CD32X_HANDOFF.md`, `port/plat_mars.c`
- `OpenJazzCD32X`: `docs/CD32X_ADDENDUM_OPENJAZZ.md`, plus the Kobo boot PDFs in its `docs/`
- `OpenLaraCD32X`: `docs/OPENLARA_CD32X_HANDOFF.md`
- `OpenTyrianCD32X`: `docs/CD32X.md`, `TWO_SH2.md`, `DIRTY.md`, `VDP_PLANES.md`
- `Kobo32X_hw`: `README.md`, `KOBO_CD32X_BOOT_PROCESS.pdf`
- `Sonic32XPlus1`: `VIC_YATSSD.md`

The lessons are in this project's own files ("here") where a line number is given.

## 1. Booting

| Lesson | Source | What SuperTux does |
|---|---|---|
| An edited boot path gave a black screen. The same disc with the boot path byte for byte as the last proven disc booted. | Hurrican handoff, "B1 design" | Boot path unchanged. `tools/check_boot.py` stops the build on any difference. |
| The boot path is a chain proven on the console: OpenLara's CD loader, then OpenJazz J19b, then Hurrican, unchanged. | Hurrican handoff; OpenJazz addendum | Same chain. The module name stays "OpenJazz 32X" and the program stays `OPENLARA.32X` on the disc. |
| The loader's quick check (3 chunks) let a half-finished copy pass, and it crashed later. | OpenLara lesson 6 | Hurrican's loader as it is. Holding A+B+C at boot forces a full copy. |
| Parking the slave SH2 in SDRAM caused many dead discs. SH2 cart reads simply wait while the 68000 copies (RV = 1). | OpenLara lesson 2 | Nothing new; the loader's trampoline is used as it is. |
| Kobo's green diagnostic colour was set to black for release. `KOBO_BOOT_TEXT` keeps the Genesis display on. | Kobo boot PDF; `hw_md.s` | Green is `0x00A0` here. The display is on (`0x8174`, `hw_md.s` line 444) for the first builds. See `STX_HANDOFF.md`. |
| Diagnostic text costs time even when hidden. Reprinting the status rows every pass dropped Kobo to 1–2 fps. | OpenLara lesson 8; OL-2b / HW5 | Unchanged: the Sub-CPU stops reprinting at `STATE 4B10`. |

## 2. COMM registers: who owns what in this boot path

The rule from OpenLara lesson 3 is one writer per register. Anything else writing a register the renderer uses freezes the game.

| Register | User | Where |
|---|---|---|
| COMM0 | SH2 commands to the 68000 (`plat_mars.c` `send_cmd`) | `port/plat_mars.c` line 198 |
| COMM2 | the command's argument | `plat_mars.c` |
| COMM4 | 0 at start; the slave writes "S_OK" when it starts; the CMD interrupt handler (never raised: `hw_md.s` writes nothing to `0xA15102`) | `port/mars/crt0_oj.s` lines 6, 733, 899–913, 1250 |
| **COMM6** | **free.** The 68000's heartbeat write was replaced by a `nop` (OpenLara: "COMM6 belongs to the renderer"). | `cd32x/subcpu/hw_md.s` line 1307 |
| COMM8 | pad / length | `plat_mars.c` |
| COMM10 | vblank count from the 68000 | `plat_mars.c` |
| COMM12 | status codes (`STATE:` row) | `plat_mars.c`, `cdloader_sh2.c` |

**YATSSD's two-CPU drawing** uses COMM4 as a master-to-slave command and COMM6 as a shared tile counter (`yatssd/dtiles.c` lines 217–218, 571–575). Both are free once the game runs, so it could work. But the platform layer already has its own master/slave job (`plat_parallel`, an uncached SDRAM struct, `plat_mars.c` lines 153–201), and see §4 for why two-CPU drawing is not the first step.

## 3. Scrolling and the screen

**Hurrican's ring.** `plat_present_scroll` uses a 384 × 200 ring at `0x200`, with a 336-byte copy after it for lines that run past the end. Its P1 linear-ring design has no "re-anchor" (B1's re-anchors cost up to 28 ms).
- Source: Hurrican handoff, "P1 design"; `plat_mars.c` lines 715–760.
- SuperTux uses the same calls.

**Palette entry 0 becomes `0x0400`** in `plat_present_scroll` (the `pending_pal` line), so black is not see-through.
- Sources: `plat_mars.c` line 743; OpenJazz addendum §6; OpenLara lesson 7.
- Two rules follow for prepared pictures:
  - never rely on palette index 0 or the value `0x0000`;
  - keep colour 0 black in every palette, because it is what a cleared screen shows (the white flash).

**The shift-register bug.** On real hardware, a line whose line-table entry ends in `0xFF` shows wrongly while the 1-pixel shift is on.
- Sources: OpenTyrian `DIRTY.md`; YATSSD `hw_32x.c` line 42.
- Tyrian avoids it with a 320 pitch. Hurrican's ring has a 384 pitch.
- With it, the entry is `0x100 + row*192 + xoff/2`. Its low byte can be `0xFF` when `xoff` is odd and `xoff/2` is 63, 127 or 191, that is, `xoff` = 127, 255 or 383.
- **What SuperTux does:** the camera never presents those three `xoff` values. It moves one pixel instead, which cannot be seen at 50/60 Hz. The host build checks every present, as Tyrian's does.
- Hurrican does not mention the bug. It may be rare enough not to have been noticed.

**Dirty rectangles, not full redraws.**
- Hurrican restores only the rectangles the buffer's last frame drew sprites into, from a clean SDRAM copy of the tile layers.
- Tyrian marks dirty columns.
- YATSSD's `draw_dirtyrect` does the same for tile maps; its flags live in the "extrafb" after the canvas.

**16-bit frame-buffer writes only.** Byte writes are unreliable. At odd edges, read, modify and write.
- Sources: Kobo rule 4; Hurrican "B1 design".

**YATSSD's pixel loops count down an unsigned `j = hw >> 2`.** A sprite less than 4 units wide after clipping runs about 4 billion times, writing over memory.
- Source: `VIC_YATSSD.md` §6.
- SuperTux skips sprites with fewer than 8 × 2 pixels on screen, as Sonic32 does.

**Sonic32 uses YATSSD through its own glue.** It defines `debug`, `nodraw`, `window_canvas_x/y`, `tm` and `draw_dirtyrect` itself, and calls `Hw32xInit`.
- Source: `VIC_YATSSD.md` §2–3.
- SuperTux must **not** call `Hw32xInit`: the platform layer owns the VDP. Instead, set `canvas_pitch = 384`, `canvas_width = 320`, `canvas_height` = the view, point the canvas at `plat_scroll_ring()`, and present with `plat_present_scroll`. `Hw32xUpdateLineTable` is replaced by the platform's present.

## 4. Speed

**Two SH2s drawing gave no gain.** OpenJazz measured 4–5 fps on one CPU and on two. Both SH2s wait on the same bus to the frame buffer and the cart, and the master has priority.
- Sources: OpenJazz addendum §8.3; Tyrian `TWO_SH2.md` ("emulators may not model the shared bus; real-hardware numbers win").
- SuperTux starts on one CPU.

**What did help** was running the slave's game logic while the master draws. Hurrican P1m does this with `plat_parallel` and `-DOJ32X_SLAVE_DRAWS=1`, on an 8 KB stack of its own. The slave's crt0 stack is only 2 KB.
- Lessons from it: draw with the frame's own camera (P1n: a torn screen and a doubled HUD otherwise), and purge the master's cache afterwards.
- SuperTux's logic is light, so this comes later, if at all.

**Code in the cart runs slowly** (slow bus, 4 KB cache). Pixel routines go in SDRAM (`.sdcode.fast`, `OJ32X_FAST_CODE`).
- Sources: OpenJazz addendum §8.2; Hurrican "B1 design".

**Background layers on the 32X are expensive.** Hurrican measured 85 ms for three full layers, so backgrounds were planned for the Genesis planes.
- Sources: Hurrican B1b; Tyrian `VDP_PLANES.md`.
- Doing so changes the 68000 / Sub-CPU programs, which means the boot stack. Both projects left it "for the very end", and so does SuperTux.
- SuperTux's background is one 320 × 240 picture scrolled slowly. Hurrican's numbers suggest a full copy costs about 28 ms. To be measured.

**Other results:**
- Keyed sprite pixels from the cart cost about 19 cycles each (Hurrican B1b).
- Frame pacing to the median of recent frame times gives steadier motion (OpenJazz `OJ32X_PACE`).

## 5. Timing, memory, loading, sound

**The clock.** The SH2's free-running timer is reset by something. The watchdog's overflow interrupt never arrives.
- The working clock is the vblank count × 16.68 ms plus the watchdog counter (178 µs steps). This is `plat_prof_us`.
- Source: OpenJazz addendum §8.4.

**Memory.**
- Working memory is the 256 KB SDRAM.
- Fragmentation is the real risk, not leaks. Allocate at level start; never call malloc on the slave.
- Hurrican had 40 KB of heap left with its largest map.
- The cart is read-only for the SH2. It is filled through the 68000 (OpenLara lesson 1).

**Loading.**
- About 30–42 KB/s: Hurrican's 829 KB stage pack took 19.8 s.
- Level packs go to cart offset `0x280000`, up to `0x170000` bytes.
- SuperTux's levels are small (101 KB compressed for all 77), so a level set loads in a few seconds.

**CD audio.**
- No CD data reads while CD audio plays: a level load stops the music first (OpenLara lesson 4; Hurrican P1b).
- Never restart a track every frame. The "still playing?" answer must not depend on the drive's slow status (OpenLara lesson 5).

**Sound effects** come in `SFX00.BIN` (Jazz pack format) and are loaded at start-up. OpenLara played one effect per frame at a fixed volume.

**High scores and saves:** commands 56–59 of the unchanged Sub-CPU program write the backup RAM file `OPENJAZZ_JJ` (Hurrican P1p).

## 6. Checks on the PC (all four game ports do this)

- A host build of the same game code, with a stand-in platform that reads the line table back the way the 32X does and saves frames as pictures (Hurrican `bench/host.c`).
- Runs under the address and undefined-behaviour checkers with random input (Hurrican: 20,000 frames).
- A reachability check (Hurrican `make -C host reach`): can the level end be reached with the game's own movement code?
