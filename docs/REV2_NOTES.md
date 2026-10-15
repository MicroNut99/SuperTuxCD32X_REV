# REV2 – the Genesis picture commands (a boot test)

REV's goal is Sonic CD's way of drawing: the level on the Genesis video chip's tile planes
(scrolled by register, no drawing), the 32X drawing only the sprites on top. The SH2 cannot reach
the Genesis video chip; only the 68000 can, and the SH2 talks to it through the Sub-CPU. The 68000
listener (`cd32x/subcpu/hw_md.s`) always had VDP routines (Chilly's `set_vram`, `copy_vram`,
`set_palette`; Kobo's `set_vdp_reg`), but no SH2 command reached them. REV2 adds those commands.

## What changed (Micronut99's decision, Oct 2026: "just add the commands", one Fusion test)
`cd32x/subcpu/main.c` – SH2 commands (COMM0), handled in the Sub-CPU's command loop:

| Cmd | Does | Parameters (written by the SH2 before COMM0) |
|---|---|---|
| 62 | one word into Genesis VRAM | COMM2 = VRAM byte address, COMM8 = the word |
| 63 | RAM cart -> VRAM (tiles, maps, the H-scroll table) | COMM2 = VRAM byte address, COMM8L = words << 22 \| cart offset (even, 1-1023 words); reply COMM12 1 / 0 |
| 64 | RAM cart -> Genesis colours (CRAM) | COMM2 = first colour 0-63, COMM8L = count << 22 \| cart offset; reply COMM12 1 / 0 |
| 65 | a VDP register | COMM2 = 0x8000 \| reg << 8 \| value (registers 0-23) |
| 66 | vertical scroll (VSRAM) | COMM2 = 0 plane A / 2 plane B, COMM8 = the scroll |

Done = COMM0 cleared, as every command. The cart is read by the 68000 through the 32X bank window
0x900000 (RV 0, like CMD 46's read-back), so the SH2 is never locked out of the cart; a copy that
crosses a 1 MB bank is split.

`cd32x/subcpu/hw_md.s` – one new 68000 routine, `set_vsram` (listener command 36,
`MD_CMD_END` 36 -> 37): VSRAM is not VRAM, so `set_vram`'s address command cannot reach it.

Nothing in the boot sequence calls any of this; the game does not call it yet either.

Boot diagnostics: ALL of them on - the full set of docs/STX_HANDOFF.md / BOOT_PATH_NOTES.md
section 7, the state R1a / R1b booted on the console with:
- `KOBO_DIAG_TEXT 1` (`cd32x/subcpu/main.c`): the green lines, `STATE:`, the heartbeat
- `KOBO_BOOT_TEXT 1` (`main.c`, already 1): main() does not switch the display off
- `SCD_DIAG_TEXT 1` (`cd32x/subcpu/scd.c`): CKPT / INITCD / CD time-outs / file not found
- `hw_md.s` `md_init_hw`: `0x8174` - the Genesis display ON from the start (Chilly's white lines)
- `OJ_DIAG_TEXT 1` (`cd32x/sh2/cdloader_sh2.c`): the text stays IN FRONT of the splash
- `-DOJ32X_DIAG_TEXT=1` (build.sh): the text stays until the game has started
(A first REV2 zip had only the first two, as DIAG_TEXT_HOWTO.md says for the OpenLara /
Cannonball stack. On this stack that hides everything: md_init_hw leaves the display off and the
loader's splash covers the text - Fusion showed only the Tux loading screen.)

`tools/check_boot.py` knows the four added blocks exactly: it takes them out and the rest of
every boot file must still be Hurrican's byte for byte (checked: one changed byte anywhere else,
or inside a block, stops the build).

## The test
Run `SuperTuxREV2.cue` in Kega Fusion (Sega CD + 32X). It must boot exactly as R11 / REV1 did,
now with the white and green text on screen, up to where Fusion always stops (no RAM cart).
If it does: the changed Sub-CPU and 68000 programs boot, and the next round makes the game use
the commands (a level's background on a Genesis plane). If it does not: the green / white text
shows where it stopped.

Not built here (no 68000 toolchain in the workspace): the Sub-CPU / 68000 programs are compiled
by build.sh on Micronut99's machine. `main.c` passed a C syntax check.
