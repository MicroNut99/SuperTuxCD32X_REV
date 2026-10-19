# SuperTux CD32X – 8 MB and 16 MB SRAM cart releases (design)

Status: **design only.** The board (MicroNut99/16MB-PCB-CD32X, rev 0.5) is routed but not built
or tested; its GreenPAK image is not public. No OpenJazz 8 / 16 MB build exists either - the Jazz
build notes, Addendum 2 and the OpenJazz repo describe the 4 MB cart only. This design uses
what IS proven on the 4 MB cart (Jazz / SuperTux REV10b) and the board's documented paging.

## 1. What the board gives us (16MB-PCB-CD32X README §2, §7, §10, §12)
- The console still sees a **4 MB cart window**, split into **two 2 MB windows**:
  window 0 = cart offset 0x000000-0x1FFFFF, window 1 = 0x200000-0x3FFFFF.
- Each window shows one of **8 pages of 2 MB** (16 MB build; pages 0-3 on the 8 MB build -
  with the 8 MB GreenPAK image pages 4-7 mirror 0-3).
  Linear SRAM address = page × 0x200000 + (address & 0x1FFFFF).
- **Page registers** (68000 only, /TIME space, write-only, even byte, page in D8-D10):
  window 0 at **0xA130F0**, window 1 at **0xA130F2**. Odd 0xA130F1 is ignored.
- **Power-on: window 0 = page 0, window 1 = page 1 = exactly the 4 MB cart** - the boot,
  the program, IMAGE8 and the saves at 0x3F0000 are where they are today.
- **All of the SRAM is battery-backed** (CR2450): whatever is copied in stays after power-off.
- Only the 68000 writes the cart (RV=1, as CMD 46 / 61 do now). The SH2s only read, cannot
  reach the page registers, and must **purge their cache for the cart** after a page change.
- Untested on the board: page-register writes with the 32X adapter on (ADEN=1); the README
  recommends writing them with RV=1 - the first thing to test.

## 2. Today's 4 MB layout (REV10b, tools/stx_pack.py)

| Cart | Contents |
|---|---|
| 0x000000 | the program (`OPENLARA.32X`, ~230 KB) |
| 0x100000 | `IMAGE8.RAW`: everything that is not a level + the title's demo (0.6 MB) |
| 0x198000-0x260000 | area A (`IMAGE9.RAW`, slot 9): Icyisland levels 1-6, kept once read |
| 0x280000-0x3F0000 | area B: a ring - the other 70 levels take turns (`OJnnn.PAK`, slot 20) |
| 0x3F0000 | the save area, 16 pieces of 4 KB (SuperTux: piece 13) |

The levels: 76 + the demo, **9.35 MB** in all (Icyisland 3.36 MB, Bonus Island 1 2.72 MB,
Bonus Island 2 3.27 MB; largest pack 298 KB). On 4 MB only 1.6 MB of them fit at once.

## 3. The design: window 0 fixed, window 1 paged

**Window 0 never changes** (page 0): the program runs from it and IMAGE8 lives in it - paging
it would pull the code out from under the SH2s. **Window 1 is switched only while a level is
being read** (the loading screen), to the page that holds that level, and stays there for the
whole level. The world map, menus and saves need page 1 (the saves are at 0x3F0000 in page 1):
leaving a level switches back to page 1.

- Each level's home becomes **(page, offset)**: HOMES.BIN gets a page byte (area 3 = "paged").
- Pages 2-7 are filled through the existing slot of `OJnnn.PAK` (CMD 38 ids 200+, cart slot
  20 = 0x280000): **1.5 MB a page** (0x280000-0x3FFFFF; 0x3F0000 is only the saves in page 1).
  No change to commands 38 / 46: the 68000 writes through window 1, whatever page it shows.

| Build | Window 0 (page 0) | Window 1 page 1 | Pages 2.. (1.5 MB each) | Room for levels | Levels |
|---|---|---|---|---|---|
| 4 MB (today) | area A to 0x200000 | area A rest + ring + saves | – | 1.6 MB at once | streamed |
| 8 MB | area A | area A rest + ring + saves | 2, 3 = 3 MB | 5.3 MB | most resident (see 5) |
| 16 MB | area A | area A rest + saves | 2-7 = 9 MB | 11.3 MB | **all 77 resident** |

## 4. New commands (boot stack ADDITIONS, never changes - ledger rule: numbers 68 and up)
- **CMD 68 - cart window 1 page.** COMM2 = page (1-7). The Sub-CPU has the 68000 write the
  page register with RV=1 (a new listener routine like REV2's `set_vsram`, ~20 bytes), reads a
  marker back through the 0x900000 window to confirm, replies COMM12 = 1 / 0. The SH2 waits
  in its SDRAM code (as for CMD 46), then purges the cache for the cart.
- **CMD 69 - cart size probe.** The 68000 writes a marker into a scratch 4 KB of pages 1, 2
  and 6 (and restores what was there), reads back: no paging = 4 MB, page 6 mirrors page 2 =
  8 MB, else 16 MB. Reply COMM12 = 4 / 8 / 16. Run once at start-up.
- Both added to `tools/check_boot.py` as exact blocks, to the docs repo's ledger, and tested
  first on their own (as REV2 was): a test disc that probes the cart and writes / reads a
  pattern on every page, results on the green diagnostics text.

## 5. One disc, three cart sizes
The same disc runs on the 4 MB cart and both boards: CMD 69 decides at start-up which
HOMES table to use (all three are in IMAGE8, ~1 KB each). A 4 MB cart behaves exactly as
REV10b (the page writes go nowhere on it - the probe confirms it before anything is paged).

- **16 MB:** every level has its own home; once read it is never read again.
- **8 MB:** needs step 0 below. With it, Icyisland (2.6 MB) and Bonus Island 1 (2.1 MB) get
  homes of their own (area A, pages 2-3, part of page 1) and Bonus Island 2 shares the rest of
  page 1 as a ring, as today. Without step 0 they do not fit (6.1 MB for 5.35 MB of room).

## 6. Kept after power-off ("install once")
The SRAM keeps everything, but the game forgets what it copied: `in_cart[]` is in SDRAM.
- A **cart directory** in save piece 14 ("STXD"): a build ID (the disc's data hash) and one
  bit per level "installed", written (CMD 58 + 61, as the saves) after each level copied in.
- At start-up: directory read straight from 0x223FE000; a different build ID = nothing
  installed (a new disc never trusts old data). Each level read also checks its pack header
  (magic, size, build ID) before use - a bad one is simply read again from the disc.
- **A+B+C held at power-on** forgets the directory (Jazz's remedy for a mixed cart).
- Optional: *Options → Install all levels* - copies every level in one go (about 5 minutes at
  the ~30 KB/s CMD 46 manages, Jazz measurement), after which the disc is only read for music.

## 7. Step 0 - smaller levels for every cart (no new hardware needed)
Since REV6 the 32X never draws level tiles, but each pack still carries `COMPG.BIN`'s picture
blocks (2.1 MB of the 9.35 MB; only its first 128 bytes, the sky colours, are used). Leaving
them out: **7.2 MB of levels**, 23 % less to read from the disc on every cart, and the 8 MB
board holds all but ~2 MB. (`LEVELG.BIN`, 4.2 MB, is the next candidate: the map layers
compress ~20:1, but unpacking needs SDRAM.)

## 8. Order of work
1. Step 0 (COMPG blocks out) - a normal disc, tested on the 4 MB cart.
2. CMD 68 / 69 + test disc - needs a built board; Fusion cannot emulate any of this.
3. HOMES per cart size, paging at level load, cart directory - PC build models pages 0-7.
4. Releases: one disc; README states which cart sizes have been tested on hardware.

## 9. Open points
- Do page-register writes work with ADEN=1 / RV=1 on the real board (board README §12)?
- The GreenPAK image (private) decides how an 8 MB board answers pages 4-7 - the probe must
  handle both "mirror" and "undefined".
- Save piece 14 for the directory must be added to the docs repo's save-piece table.
