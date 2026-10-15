# REV9 – the sky without bands, and a fourth palette for the level

**The sky without bands** (`tools/stx_pack.py sky_rgb / sky_dither`, `game/gen_level.c sky_fill`).
15-bit colour has 32 steps a channel, so a smooth gradient shows steps - horizontal lines where a
channel goes up one. REV9 uses the usual trick: each line's exact colour is mixed from the two
nearest 15-bit steps in a 4 × 4 ordered pattern (Bayer); on a TV the mix reads as the colour in
between, and the steps disappear.
- Each level's pack has `SKY.BIN`: for each of the view's 176 lines, 4 palette indices (the
  pattern's pixels 0-3; the pattern's row is the line's y & 3). The colours themselves are the
  level's palette 192-255 (at most 64; the most any level needs is 62), each with the through
  bit as in REV8.
- `sky_fill` writes a line's pattern a 32-bit word at a time, like a plain fill: same speed as
  REV8.
- The colours: SuperTux's own gradient, or the picture's own colours (top, middle and bottom
  thirds joined smoothly), now worked out for each line (REV8: 64 shades).

**A fourth palette for the level** (`tools/stx_genesis.py`). With the Genesis sky off (REV8),
palette 3 was unused: the level's tiles now get 4 palettes of 15 colours (60) instead of 3 (45).

No boot stack changes.

## Checked on the PC
- All 77 levels, 900 frames of random play, against the reference (whole 32X layer cleared,
  every column written each frame): identical. All 77 levels, 3000 frames, address /
  undefined-behaviour checkers: clean.
- The SH2 build; `tools/check_boot.py`.
