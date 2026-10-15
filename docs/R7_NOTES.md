# R7 - the up / down re-anchor (the dips while jumping)

(R6: docs/R6_NOTES.md.) Micronut99's R6 video, Welcome to Antarctica: F 45-60 running on the
ground, but **F 33.1 (T 14.6, N 24)** and **F 40.1 (T 11.1, N 19)** when Tux was up on the
platforms and the view went up and down with him.

## Why

The canvas has only 16 lines more than the view, so the view re-anchors every 16 lines up or
down (and every 64 pixels sideways), in each of the two buffers. On the PC, 1170 frames of
running and jumping: 54 sideways, 56 up / down and 4 diagonal re-anchors. Since R4 a re-anchor
MOVES the canvas's pixels (74 KB); the sideways moves were shared by both SH2s, but the up / down
ones were done by one SH2 alone (the lines overlap). A move reads the 32X frame buffer, which is
slow. T 11-15 ms over 16 frames with N 19-24 fits several such moves.

## What R7 does (game/yt_layers.c reanchor)

- An up / down move is now shared by both SH2s too: by columns (each moves every line, half the
  columns, in the safe order).
- Move or redraw, measured on the console: the tiler times what a cell costs (frames with 32
  cells or more) and each kind of move is timed (sideways; with up / down). A re-anchor redraws
  the view (about 252 cells, both SH2s, the frame buffer only written) when that is cheaper
  than the last moves of that kind took, and moves otherwise; every 16th time it moves again to
  keep the measurement current. Nobody had measured the two on the console; now the console
  decides for itself.
- Both ways give the same picture (PC: ST_REANCHOR=move / draw forces one; Welcome to
  Antarctica, Via Nostalgica and Above the Arctic Skies compared frame by frame: identical).

## Checked here

- All 77 levels under the address / undefined-behaviour checkers, BACKGROUND FIXED and MOVES.
- SH2 build, check_sdram_code.py, check_boot.py (release, 70 files identical).

## What to look at on the console

- Welcome to Antarctica: jumping onto the high platforms and the coin rows (the F 33-40 spots of
  the R6 video). F and T there against R6.
- Running on the ground: F as in R6.
