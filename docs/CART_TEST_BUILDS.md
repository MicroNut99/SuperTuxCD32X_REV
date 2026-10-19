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
- **Options → CART TEST** (from the title only): the probe again, then a 4 KB pattern written to
  piece 15 of every page and read back by the SH2 (`PAGE n OK / READ BACK BAD / WRITE FAILED`),
  then page 1 again and **`SAVES UNTOUCHED`** (piece 13 compared before / after).
- **Levels** (16 MB): 61 levels in pages 2-7 and 9 in page 1 - every level has its own place.
  8 MB: 20 levels in pages 2-3, the other 50 share page 1's ring as on the 4 MB cart. A level is
  read from the disc once; after that (also after power-off: the cart is battery-backed) it is
  used straight from the cart **if its sum matches `SUMS.BIN`** - otherwise read again.
- **A smaller cart than the disc** (or no mapper): the levels of missing pages use page 1, as
  on the 4 MB cart - the game still plays.
- Window 0 (the program) is never paged; the saves stay on page 1 (every save switches window 1
  to page 1 and back); after each page change both SH2s purge their caches.

## Order of the tests
1. **4 MB cart, `SuperTuxREV10b_16MB`:** boots? Title says `CART 4 MB`? CART TEST:
   `CART 4 MB`, `SAVES UNTOUCHED`; a level plays; saving works. If it does not boot, or the
   title never comes, the page-register write is a show-stopper on the 4 MB cart - stop there.
2. **8 MB board:** title `CART 8 MB`; CART TEST pages 2-3 OK.
3. **16 MB board:** title `CART 16 MB`; CART TEST pages 2-7 OK; play levels from several worlds,
   power off and on, play them again - no loading screen for a level already played.

## Checked on the PC (game/host.c models the board: ST_CARTMB=4 / 8 / 16)
- Probe and CART TEST: 16 → pages 2-7 OK; 8 (pages 4-7 = 0-3) → pages 2-3 OK; 4 → nothing
  paged; saves untouched in all three.
- Levels switch window 1 to their page before reading; a level from a page the cart lacks uses
  page 1. Power-off kept (ST_CARTIMG): levels come from the cart, a damaged one is read again.
- The normal build's data: byte-identical to REV10b's.
