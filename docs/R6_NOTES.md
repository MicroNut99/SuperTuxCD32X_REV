# R6 - smooth motion: drawing between steps

(R5: docs/R5_NOTES.md.) Micronut99's R5 video on the console, Welcome to Antarctica, with the
timing line: **F 47-60, L 1.7-3.0, T 2.7-6.6, S 0.7-1.3, H 1.1-3.2, W 4.7-10, N 6-32** - and
"Not smooth at all."

## Why it was not smooth at almost 60 frames a second

The world moves in SuperTux's own 10 ms steps (frame_ratio 1, 100 steps a second; every speed,
timer and jump in tux.c / objects.c is written for it). The 32X shows a picture at each 60 Hz
refresh, 16.7 ms apart. So a picture gets 1 or 2 steps - 2, 2, 1, 2, 2, 1 ... - and between
two pictures the world moved 20, 20, 10 ms: the view, Tux and everything else jerk forward
and stall, three times a cycle, even at a steady 60. On the PC with even 17 ms frames the
camera moved 4, 1, 3, 4, 1 pixels a frame running right; it now moves 2, 3, 3, 2, 3.

(Before R6 this was hidden by the frame rate: at 17-22 frames a second every picture had 4-6
steps and the unevenness was small next to the jumps.)

## What R6 does (game.c, objects.c, wmap.c, stx.h lerp_fx)

Before each step (`world_action` -> `interp_save`, `objects_interp_save`) the positions are
kept: the camera (scroll_x, cam_y), Tux, the snow / clouds, and every bad guy, upgrade, bullet,
bouncing coin, bouncing brick, brick piece and floating score. `step_clock` leaves the time
not yet stepped (0-9 ms); `interp_a` = that / 10 ms x 256. A picture draws everything at
prev + (now - prev) x interp_a / 256 - so it shows the world at the clock's time, one step
(10 ms) behind, and everything moves evenly. The rules are unchanged: only where things are
DRAWN moves.

- A position that jumps more than 64 units in a step (a reset point, a respawn, snow wrapping to
  the top) is drawn where it is; something not in use before the step is drawn where it is.
- `world_start` switches it off until the first step (a new level, a restart).
- The world map: Tux's place before each step, drawn between the same way (`wmap.c ip_x`).
- The title's demo uses the level's drawing, so it is smooth too.
- Cost: copying about 40 positions a step and a multiply each when drawing - nothing next to
  the drawing. SDRAM: 29.3 KB heap left (nothing in the game mallocs on the 32X).

R5's speed-ups are all kept.

## Checked here

- All 77 levels, 3000 frames of random input, address / undefined-behaviour checkers, with
  BACKGROUND FIXED and MOVES: no errors. The world map: no errors.
- SH2 build, `check_sdram_code.py`, `check_boot.py` (release, 70 files identical).

## What to look at on the console

- Running right on Welcome to Antarctica: the view and Tux gliding, not stepping.
- Jumping, bad guys walking, a coin bouncing out of a box, snow falling.
- A reset point after dying (should appear at once, no slide), the world map walk.
