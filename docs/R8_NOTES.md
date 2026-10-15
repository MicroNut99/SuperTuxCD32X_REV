# R8 - jumping with A

(R7: docs/R7_NOTES.md.) Micronut99 on R7: "smoother but can no longer jump. Tux jumps a tiny bit
but thats it" - and: "Its started happening with the A button in the previous build".

## What was found

- On the PC, R5, R6 and R7 jump exactly the same with A and with C (jump.cpp's height: Tux's y
  416 -> 277, with or without running). The game reads A and C with the same test
  (`tux.in_up = pad & (PAD_C | PAD_A)`), so the game's rules cannot tell them apart.
- A jump that is let go stops climbing at once (handle_vertical: `vy = 0`, SuperTux's short
  jump). A "tiny jump" is what one pad read without the button does just after take-off.
- So on the console the A bit drops out for a read now and then. A is read in the pad's TH-low
  phase together with START, the phase the 6-button pad's read sequence changes; C is read
  in the other phase. The pad reading itself is the boot path's (cd32x/subcpu/hw_md.s
  get_pad, the 68000's vblank copy), which stays as it is.

## What R8 does (game/game.c read_input)

- The jump counts as let go only after two pad reads in a row without A or C. A single
  missing read no longer cuts the jump; a real let-go is seen one frame later (about 17 ms;
  the short hop still works). PC check: A held with one-frame gaps 6 and 12 frames after the
  take-off now jumps the full height (y 277); a quick tap still gives a short hop (y 376).
- With OPTIONS - TIMING on, the panel's second line ends in the raw pad as the console sent
  it: `P` and four hex digits (B 0010, C 0020, A 0040, START 0080; UP 0001, DOWN 0002, LEFT
  0004, RIGHT 0008). Holding A should show 0040 steadily; if it flickers, that is the pad
  read.

## Checked here

- All 77 levels under the address / undefined-behaviour checkers, BACKGROUND FIXED and MOVES.
- SH2 build, check_sdram_code.py, check_boot.py (release, 70 files identical).
