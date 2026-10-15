# REV1 – the first round of the rip and rebuild

REV is the rip and rebuild of SuperTux CD32X's drawing, aimed at the speed Sega CD games reach on
the same hardware: a steady 60 frames a second, no catch when the view scrolls (README.md, top).
REV1 starts from R11 (commit `0a38bbf`, the R11 source unchanged).

## Where R11 stood
- 45–60 frames a second on the console (Options → Timing, Icyisland).
- A visible hitch at every sideways shift of the canvas (every 48–64 pixels; R10 made it
  smaller and moves it to a quiet frame when it can).
- The averages on the timing line (16 frames) hide a single slow frame, so the hitch's cost has
  never been measured on the console. That number decides what the next round rips out.

## What the docs set (github.com/MicroNut99/docs, read in full for this round)
- **The boot path stays.** One proven chain: SH2 → 32X COMM → Sub-CPU → Sega CD comm → the 68000
  listener. Everything below the game is copied byte for byte (`tools/check_boot.py`). So the
  Mega-CD's own hardware (Word RAM, the graphics ASIC that writes Genesis tiles, Genesis plane
  scrolling) cannot be used without boot changes, and that is not on the table. REV rebuilds on the
  two SH2s.
- **DMA for opaque lines** (OpenTyrian addendum, "what made it fast", point 4): compose a line
  in RAM, let the SH2's DMA channel 0 copy it (cycle-steal, 32-bit units) while the CPU composes
  the next; measured 18–19 ms against 24 ms; a timeout on every wait and the CPU for good if one
  fails. On the PC the DMA is a memcpy, so only the console shows whether it pays.
- **A second SH2 does not help drawing much** (Tyrian point 6, Jazz notes): both share the path
  to the frame buffer. Measure before guessing (Jazz: "a small timing display ... showing how long
  each part of a frame takes").
- **32X manual, 1.15 Restrictions:** interrupts masked while an auto-request DMA runs; never DMA on
  both SH2s at once; not with PWM or an H interrupt. Memory-to-memory CHCR: `xxxx xx10 1110 0xxx`.
- **Fusion** boots CD32X discs but has no RAM cart (the game stops at the splash); ares does not
  boot CD32X. Console tests are discs, so a round carries everything that needs measuring.

## What REV1 changed
### 1. Options → Draw: CPU / DMA (`game/yt_layers.c`, `game/yt_mars.c`)
- The tiler lists the cells to draw as before. With **DMA**, adjacent cells on the same canvas
  line (at least 3: `DMA_RUN_MIN`) form a **row**. For each of a row's 16 lines the master
  copies 16 pixels from each cell's picture (the cart, through the cache) into a line buffer in
  SDRAM, starts the DMA unit on it, and puts the next line together in the other buffer while
  the DMA copies. Rows wrapping the RING at line 199 are split per line, so they need nothing
  special.
- Rows come from: a row of cells coming in at the top or bottom (RING), the cells that differ
  after a shift that left the canvas in place, a new level. On Icyisland's levels about two
  thirds of all cells drawn are in rows (PC, level 3: 10404 of 15380).
- The other cells (a column coming in at the side, single cells) are drawn with Vic's pixel loop
  as in R11, by the slave while the master runs its rows; when the rows are less than half of
  the cells, the master also takes singles until both have about half.
- Fewer than 5 cells in a frame: the master alone, as R11.
- The DMA unit (`plat_dma_start`, `plat_dma_wait`, `plat_dma_ok`, declared in `port/plat.h` since
  Tyrian, defined by nothing in the port; REV1 defines them in `game/yt_mars.c`, so `port/` stays
  unchanged):
  - channel 0, auto request, cycle-steal, longs, both addresses incrementing, `CHCR = 0x5AE1`,
    `DRCR0 = 0`, `DMAOR = 1`; source and destination given through the cache-through area;
  - the master's interrupts are masked from the start of a line's transfer to the end of its
    wait (manual 1.15), and opened between lines – a vblank waits at most one line;
  - every wait has a limit (~30 ms; a line takes well under 0.2 ms). If a transfer never ends
    the channel is stopped, that line is copied by the CPU, and DMA is not used again until
    power-off (`D!` on the panel).
- Default: **CPU** (R11's way), so the disc behaves like R11 until it is switched. Saved with
  the options (backup RAM byte 5, bit 5).

### 2. Options → Timing: Off / On / Worst (`game/game.c`)
- **Worst** latches the slowest frame of the last 64: `X` (L + T + S + H), its `L T S H`, `M`
  (the shift in that frame: `M-` none, `Mn.n` the canvas moved in n.n ms, `ML` left in place and
  the differing cells drawn), `N` cells, `D` cells in DMA rows. Saved (byte 5, bit 4).
- The panel's second line ends in `D0` / `D1` / `D!` while the timing is on.

## Checked on the PC
- All 77 levels, 900 frames each (every 3rd compared), random play: **DMA against CPU – identical
  pictures** in RING and CLASSIC; against `ST_TRUTH` (every frame drawn from nothing) the
  same 11 levels differ by the same pixels with DMA as with the CPU (they already did in R11:
  the kept clouds and far snow, which ST_TRUTH draws differently on purpose).
- `ST_DMA_FAIL=40`: the 40th transfer "fails" – the rest is drawn by the CPU, pictures still
  identical on all 77 levels.
- The three world maps: DMA against CPU identical.
- All 77 levels, 3000 frames, under the address and undefined-behaviour checkers, DMA and
  Timing Worst on, RING and CLASSIC: clean.
- The SH2 build: no library calls in the new code (checked in the disassembly);
  `tools/check_boot.py`: the 70 boot files identical.
- PC switches: `ST_DMA=1` Draw: DMA, `ST_TIMING=2` Timing: Worst, `ST_DMA_FAIL=n`; `ST_STATS`
  prints the DMA row cells as its last number.

## What to test on the console (one disc)
Icyisland level 1 (and a cloud level, e.g. Above the Arctic Skies) with *Timing: Worst*, running
right with B held, for about 20 seconds each:
1. **Draw: CPU** – write down the usual X, T, M and N of the worst frames. This is the hitch,
   measured for the first time.
2. **Draw: DMA** – the same run. Does X / T drop? Does the panel stay at `D1` (not `D!`)?
   Any wrong pixels in new rows of tiles (top and bottom when jumping, after the sideways shift)?
3. *Timing: On*, CPU against DMA – the averages (F, T).
4. Music and sound effects with DMA on (the DMA masks the master's interrupts for a line at a
   time; the CD music and PCM effects are on the Sega CD side, so nothing should change).

What the numbers decide:
- If `M` is several ms on the worst frames (the move), the next round replaces the move.
- If `ML` frames are worst with a big `T`, the cells after the shift are the cost: spreading them
  over the frames before the shift (already drawing the coming column into the canvas's spare
  columns ahead of time) is the next round.
- If DMA lowers T, it becomes the default; if it does not, it goes (Tyrian's measurement was
  for a different kind of drawing).
