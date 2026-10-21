# REV11-16MB-T1 - the whole game in the 16 MB cart

`CART_MB=16 ./build.sh` -> `cd32x/subcpu/SuperTuxREV11-16MB-T1.cue / .bin`. Built on REV11-T1
(even motion) + the 8 / 16 MB cart work (command 68, `docs/CART_TEST_BUILDS.md`). The plain
`./build.sh` makes `SuperTuxREV11-T2` (4 MB: REV11-T1 with the cart code compiled out).

## What it does
- **Every level has its own place for good:** all 70 level packs (299 pieces of 32 KB, 9.3 MB)
  in pages 1-7 of the 16 MB cart, 0x280000-0x3EFFFF of each page, packed largest first
  (`tools/stx_pack.py --cart-mb 16`): pages 2-7 full (45-46 of 46 pieces), page 1 - which other
  discs' level rings also use - the fewest (26). Icyisland 1-7 stay in area A (IMAGE9, read at
  every start as before).
- **First start on a 16 MB cart:** after the usual loading bar, a screen "16 MB CART - COPYING THE
  LEVELS - ONLY THE FIRST TIME", level n of 70 and a bar (`game/cart.c cart_preload`).
- **Every later start:** each level's sum is checked against `SUMS.BIN` (no visible time); only a
  missing or damaged level is copied again (another disc that used page 1, a power-off during the
  copy). After that **no level shows a loading screen.**
- **A smaller cart** (4 MB, or 8 MB): no copying; the levels are read when played, as before.
- The title: `SUPERTUX REV11-16MB-T1 - CART 16 MB`.

## Checked in ares (MicroNut99/ares_android, headless runner, the disc's data track)
| | |
|---|---|
| Blank 16 MB cart, first start | the copy screen counts the levels; ~3.5-4 min (each 32 KB piece is its own disc read) |
| Power-off at level 50 of 70, start again | copies only the 17 missing, then the title |
| Third start | no copy screen, title as fast as usual; Level Select 1-10 "23rd Airborne": straight to its intro, no LOADING |
| The same disc on a 4 MB cart | title `CART 4 MB`, no copying, a level shows its loading bar and plays |
| Test 3 (REV10b-16MB-T3) built here | no stop after the loading bar on 4 MB and 16 MB carts (the console's stop was test 2's disc) |

Still to see on the console: the page register on the real board (command 68 is the boot-stack
addition agreed for these builds - `tools/check_boot.py` accepts exactly that block), and how long
the first copy takes there.
