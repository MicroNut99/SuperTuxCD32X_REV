# R10 - the ring: no more up / down shifts

(R9: docs/R9_NOTES.md.) Micronut99, after R9: "smooth is better. sometimes the background feels
like it doesnt keep up". Spreading a shift over several frames is not possible here (the
platform layer shows the whole view with ONE scroll offset, so a half-shifted canvas would be
shown half shifted); what was done instead - his choice "1-3, go":

## 1. OPTIONS - SCROLL <RING> / <CLASSIC> (RING by default)

The platform layer's line table already wraps the canvas up / down: view line g shows canvas line
(row0 + g) % 200 (port/plat_mars.c plat_present_scroll). RING puts world line L at canvas line
L % 200, so row0 = cam_y % 200 and the view simply moves round the ring: when the camera goes up
or down only the row of cells coming in is drawn - nothing is ever moved up / down again. A row
of cells starts at (16 ty) % 200, a multiple of 8; the row that starts at line 192 is drawn in two
parts (lines 192-199 and 0-7), and so is any sprite over line 199 (game/yt_layers.c draw_cell,
yt_sprite). The line table never ends in 0xFF with a 384 pitch and a window x under 64, wrapping
or not.

PC, Welcome to Antarctica, 1080 frames of running and jumping: CLASSIC (= R9) 34 sideways, 74 up
/ down and 6 diagonal re-anchors; RING 0 up / down. The cells drawn a frame are the same.
CLASSIC is the R9 way (YATSSD's), kept to compare on the console. Saved: bit 3 of the options
byte b[5] (1 = CLASSIC).

## 2. Our own record of the canvas (and a cheaper sideways re-anchor)

Vic's dirty matrix (16-line cells in the frame buffer, read slowly) is replaced by a record in
SDRAM: for each 8-line band of each of the 24 columns, which picture and which half of it the
band shows (game/yt_layers.c, "THE RECORD"). 8 lines because the ring's rows start on 8-line
steps. A cell is drawn when either of its two bands does not show its half. When the measured
choice (R7) says "redraw" rather than "move", the canvas is now LEFT as it is with its record:
the tiler draws only the cells whose picture differs at their new place (R7 marked every cell
unknown first and drew them all).

## 3. Sideways re-anchors early, on a quiet frame

The anchor (which world column the canvas starts at) is now per buffer and free: kept while the
view fits (window x 0..63), moved 3 cells EARLY when the view is in the last 16 pixels on its way
and the last frame drew fewer than 16 cells, or 3-4 cells when it must. The early ones mean about
a third more sideways re-anchors, each on a frame with time to spare.

## Found and fixed on the way

- A 16-line strip of old picture (R4-R9, CLASSIC): when the view went sideways AND up / down in
  the same frame, the move did the upper half of the canvas first also when the picture moved
  down, and the lower half then copied lines that were already overwritten. Seen in R9 on the PC
  on Welcome to Antarctica and on the title; possibly the "background doesn't keep up".
- Clouds after a re-anchor that left the canvas in place (R7-R9): the cloud pixels stayed where
  they were but stood for another place in the world. Each buffer now has a pixel "epoch"
  (yt_epoch) - the clouds are drawn again whole when it changes - and kept pixels are flagged in
  the record so their bands are drawn clean.
- A cloud left with 16 x 8 holes when the tiler drew a cell one of whose bands had been right
  already (the record did not change, so the cloud did not know): bands drawn this frame now count
  as changed for the clouds.
- BACKGROUND MOVES no longer moves the canvas at a re-anchor (it draws the whole view anyway).

## Checked here

- New PC reference, ST_TRUTH=1: every frame drawn from nothing. RING and CLASSIC, moving and
  leaving the canvas (ST_REANCHOR=move / draw), BACKGROUND MOVES: every third frame of 13 levels
  (snow, clouds, the waterfall level, bonus islands) compared pixel by pixel - identical, except
  coins / bonus boxes over clouds on three Bonus Island levels, which differ in their animation
  phase only (the tiles drawn over a cloud use SuperTux's own animation timing; a full redraw
  every frame just shows another phase - as since R5).
- The title and the world map against R9: identical except R9's strip (R10 matches the reference).
- All 77 levels under the address / undefined-behaviour checkers in RING, CLASSIC and BACKGROUND
  MOVES; SH2 build (heap 18.6 KB, nothing in the game mallocs on the 32X), check_sdram_code.py,
  check_boot.py (release, 70 files identical).

## What to look at on the console

- Welcome to Antarctica, jumping onto the high platforms: no catch when the view goes up and
  down (RING). Then OPTIONS - SCROLL CLASSIC for comparison.
- Running right: the sideways catches (every 48-64 pixels).
- Above the Arctic Skies (clouds): clouds whole, no holes, no strips.
