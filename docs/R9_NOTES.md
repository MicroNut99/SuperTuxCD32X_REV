# R9 - finding the stuck buttons

(R8: docs/R8_NOTES.md.) Micronut99 on R8: "the buttons are still stuck. They start working a
little bit when the timing is turned on" (and earlier: it started with A in R6).

## What is known

- The game's rules read the pad the same way since R1; on the PC every button works in R5-R8
  (A and C jump the full height, every frame time from 16 to 50 ms).
- R6 changed only where things are DRAWN (between steps); R7 only the canvas re-anchor; R8 the
  jump let-go (two reads). None of them touches the pad.
- The pad comes from the boot path: the 68000 reads it at its vblank (cd32x/subcpu/hw_md.s
  vblank_int, get_pad), the Sub-CPU answers the SH2's CMD 48 with that copy and the 68000's
  frame counter (main.c K32, COMM8/COMM10). port/plat_mars.c plat_read_pad sends CMD 48 at
  most every 4 ms and keeps the last pad when the answer does not come.
- "Better with the timing line on": the timing line makes some frames slower (the panel is
  drawn again when its text changes). So the faster the game runs, the worse the buttons -
  something in the pad's way between the 68000, the Sub-CPU and the SH2, which R5-R7 made run
  at up to 60 requests a second.

## What R9 adds to find it (game code only; the boot path stays)

- **S on the panel** (OPTIONS - TIMING on, line 2, after P): how many pad replies came back
  OLD - a refresh or more had passed since the last request but the 68000's counter in the
  reply had not moved (game.c pad_read, yt_mars.c yt_pad_tick). If S climbs while the buttons
  feel stuck, the replies are stale and the fix belongs to the pad's way; if S stays at 0,
  the pad arrives fresh and the cause is elsewhere.
- **OPTIONS - MOTION <SMOOTH> / <STEPS>**: R6's drawing between steps on or off (saved, bit 2
  of the options byte b[5], off = STEPS). STEPS draws as R5 did. Comparing the two answers
  whether R6 has anything to do with it.
- R8's two-read let-go stays.

## Checked here

- All 77 levels under the address / undefined-behaviour checkers, BACKGROUND FIXED and MOVES.
- The options page with the new line; the panel line 2 ("... P0058 S0").
- SH2 build, check_sdram_code.py, check_boot.py (release, 70 files identical).
