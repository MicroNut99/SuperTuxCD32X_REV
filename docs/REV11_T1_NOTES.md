# REV11-T1 - even motion (the title demo's jitter)

Built from REV10b (4 MB cart). Not the 8 / 16 MB test code. Boot stack untouched
(`tools/check_boot.py` passes); game code only.

## The jitter
Found in ares (MicroNut99/ares_android's headless runner) by reading the game's own variables
from the 32X's memory every frame - no console profiling. Tux stood still on the screen (x 120 in
every frame); the **view** moved unevenly: 4, 4, 3, 5, 3, 4 px a frame instead of 4, 4, 4, 3.
The level behind Tux jumping 5 px then 3 reads as Tux jittering.

**Cause:** the step clock (`step_clock`) read the time whenever the loop got to it. In the title
the work before it - the Genesis scroll and the map columns streamed after the swap
(`gl_after_flip`) - takes a different time each frame, and the console's clock counts in vblanks,
so a frame's time came out 1/60 s short or long now and then. The game loop has the same pattern.

**Fix:** `game_wait_flip()` (game/game.c) waits for the swap as before and notes the time then;
the step clock uses that time. Frames are shown at vblanks, so the time between two swaps is what
the view should move by. Every `plat_wait_flip()` in the game's code calls it.

## Measured in ares (same disc image, only the 32X program changed)
| | 5 px jumps |
|---|---|
| Title demo, 10 s: REV10b / REV11-T1 | 26 / 0 |
| Welcome to Antarctica, running right: REV10b / REV11-T1 | 18 / 0 |

## Also
- The build's name is on the title: `SUPERTUX 0.1.3 - CD 32X - REV11-T1` (`STX_VERSION`,
  game/stx.h); the disc is `SuperTuxREV11-T1.cue / .bin`; the zip and the git tag have the same name.
- 32X work RAM: +144 bytes of `.data` (heap ~10.5 KB; the start-up check failed at 5.2 KB).
