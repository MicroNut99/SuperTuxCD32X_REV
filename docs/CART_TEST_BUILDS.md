# 8 MB / 16 MB test builds (16MB-PCB-CD32X)

**Status: TEST BUILDS - untested on hardware.** For the console tests of the paged cart
(docs repo `CD32X_CART_PAGING_PROPOSAL.md`, merged form). The normal build (no `CART_MB`) is
REV10b, unchanged: same data byte for byte, the Sub-CPU program without command 68.

## Building
```
CART_MB=16 ./build.sh      ->  cd32x/subcpu/SuperTuxREV10b_16MB.cue / .bin
CART_MB=8  ./build.sh      ->  cd32x/subcpu/SuperTuxREV10b_8MB.cue / .bin
./build.sh                 ->  cd32x/subcpu/SuperTuxREV10b.cue (4 MB, as before)
```
The switch sets, for these builds only:
- **Sub-CPU:** `-DSTX_CART_PAGED=1` → command 68 (window 1's page, 1-7): the 68000 writes the
  even byte 0xA130F2 with MD 26 `safe_cart_xfer` (interrupts off, RV = 1). `tools/check_boot.py`
  accepts exactly that block (fingerprinted), inside `#if STX_CART_PAGED`.
- **SH2 game:** `-DSTX_CART_MB=8|16` (game/cart.c, level.c, menus.c, yt_mars.c).
- **Data:** `stx_pack.py --cart-mb 8|16` - levels in pages 2-3 / 2-7, `SUMS.BIN`.

## What the disc does
- **At start-up (title):** probes the cart - markers + inverse in save piece 15 of pages 1, 2
  and 6 (commands 68 + 58 + 61), read back. The title's panel shows **`CART 4 / 8 / 16 MB`**.
  This is the first page-register write the console sees: on the 4 MB cart it tests whether the
  cart ignores 0xA130F2.
- *(Options → CART TEST was removed on 20 Oct 2026 at the owner's request; the title's
  `CART n MB` is the check.)*
- **Levels** (16 MB): 61 levels in pages 2-7 and 9 in page 1 - every level has its own place.
  8 MB: 20 levels in pages 2-3, the other 50 share page 1's ring as on the 4 MB cart. A level is
  read from the disc once; after that (also after power-off: the cart is battery-backed) it is
  used straight from the cart **if its sum matches `SUMS.BIN`** - otherwise read again.
- **A smaller cart than the disc** (or no mapper): the levels of missing pages use page 1, as
  on the 4 MB cart - the game still plays.
- Window 0 (the program) is never paged; the saves stay on page 1 (every save switches window 1
  to page 1 and back); after each page change both SH2s purge their caches.

## Order of the tests
1. **4 MB cart, `SuperTuxREV10b_16MB`:** boots? Title says `CART 4 MB` with no stop after the
   loading bar? A level plays; saving works. If it does not boot, or the
   title never comes, the page-register write is a show-stopper on the 4 MB cart - stop there.
2. **8 MB board:** title `CART 8 MB`; levels from pages 2-3 play.
3. **16 MB board:** title `CART 16 MB`; play levels from several worlds,
   power off and on, play them again - no loading screen for a level already played.

## Console test 1 (19 Oct 2026, 4 MB cart, `SuperTuxREV10b_16MB`) - stopped before the title
The disc stopped on the port's start-up check screen (`OPENJAZZ 32X - BOOT TEST`, shown only
when a check fails): **`C++ NEW/DELETE: FAILED`, `HEAP 3200 USED, PEAK 3200 OF 5248`**. Not the
page register: the game never got that far. The test build had taken ~5.4 KB of the 32X's
SDRAM - a 4 KB copy of the saves in the cart test (bss) and the paging code in `game/level.c`,
which runs from SDRAM (`.data.hot_level`) - so the heap left for the start-up check shrank from
~10.6 KB to 5.2 KB. **Fixed:** the cart test sums the save area instead of copying it, its lines
are on the stack, the checksum check is in `game/cart.c` (cart code) and level.c's paging
helpers are in `.text.paged` (cart, not SDRAM). The 16 MB build now uses 80 bytes of SDRAM more
than REV10b (`.data` 51328 vs 51248, `.bss` the same).

## Console test 2 (19 Oct 2026, 4 MB cart, `SuperTuxREV10b_16MB` rebuilt from the fix)
Boots past the start-up check and the game plays. CART TEST (owner's photo): `CART 4 MB (DISC
16 MB)`, `SAVES UNTOUCHED`. **But** the owner saw a stop of about 2 minutes after the power-on
loading bar, and CART TEST took about 2 minutes too.

**Cause (my build script):** `build.sh` passed `-DSTX_CART_PAGED=1` in the Sub-CPU Makefile's
`CCFLAGS`, but `main.c` is compiled with `HWFLAGS`. So **command 68 was never in the Sub-CPU
program** on test discs 1 and 2: the SH2 sent it, nothing answered, and the SH2 sat out its
wait (2,000,000 x 64 loops, ~50 s each) before going on with `CART 4 MB`. **This also means
the page register was never written on the console - test 2 did NOT show that the write is
harmless on the 4 MB cart** (OpenLara's disc did, for its own build).

**Fixed (20 Oct 2026):** `build.sh` passes the switch in `HWFLAGS` (same flags as before + the
switch) and stops if main.c's compile line does not carry it. CART TEST removed (owner's
request).

## Console test 3 (to do: 4 MB cart, `SuperTuxREV10b_16MB` from the fix)
The first disc that really writes 0xA130F2. Expected: no stop after the loading bar, title
`CART 4 MB`, levels play, saves work. If it does not reach the title, the page write is a
show-stopper on the 4 MB cart.

## Checked on the PC (game/host.c models the board: ST_CARTMB=4 / 8 / 16)
- Probe (and the removed CART TEST): 16 → pages 2-7 OK; 8 (pages 4-7 = 0-3) → pages 2-3 OK; 4 → nothing
  paged; saves untouched in all three.
- Levels switch window 1 to their page before reading; a level from a page the cart lacks uses
  page 1. Power-off kept (ST_CARTIMG): levels come from the cart, a damaged one is read again.
- The normal build's data: byte-identical to REV10b's.
