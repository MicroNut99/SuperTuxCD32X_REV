#!/usr/bin/env python3
"""check_boot.py - proves the boot path is Hurrican CD32X's, unchanged.

    python3 tools/check_boot.py

Every file listed in docs/BOOT_FILES.md5 (the Hurrican CD32X boot path: cd32x/, port/,
make_oj_rom.py, make_boot_image.py, mars_boot.bin) must match its md5 exactly. The only
allowed differences are the documented DIAGNOSTICS switches (CD32X boot handoff: release 0,
diagnostics 1), listed below; the check puts each back to Hurrican's value before comparing,
so any other change - one byte anywhere - fails. Run by build.sh before anything is built.

Each switch may be in either state: "release" (Hurrican's line, the files byte for byte as
Hurrican's - the splash alone shows the boot) or "diagnostics" (the line below: Chilly's white
text and the Sub-CPU's green lines on). The first builds had them on; R1c is a release
(Micronut99: the splash does the job). docs/STX_HANDOFF.md says how to turn them on again.
"""
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# file -> (line as here, line as in Hurrican). Line endings are kept as in the file.
SWITCHES = {
    "cd32x/sh2/cdloader_sh2.c": [
        (b"#define OJ_DIAG_TEXT 1   /* SuperTux CD32X: diagnostics ON (was 0 in Hurrican) */",
         b"#define OJ_DIAG_TEXT 0")],
    "cd32x/subcpu/main.c": [
        (b"#define KOBO_DIAG_TEXT        1 ", b"#define KOBO_DIAG_TEXT        0 ")],
    "cd32x/subcpu/hw_md.s": [
        (b"        move.w  #0x8174,(a4)            | SuperTux CD32X first builds: display ON - Chilly's white boot text visible (OL-13 had 0x8134 = off)",
         b"        move.w  #0x8134,(a4)            | OL-13: display OFF (V-int on) - no white boot text")],
    "cd32x/subcpu/scd.c": [
        (b"#define SCD_DIAG_TEXT 1   /* SuperTux CD32X: diagnostics ON (was 0 in Hurrican) */",
         b"#define SCD_DIAG_TEXT 0")],
}

# REV2 / REV4 (SuperTux CD32X REV): the Genesis picture commands 62-67 (cd32x/subcpu/main.c) and
# the 68000's set_vsram (36) and copy_vram_col (37) (cd32x/subcpu/hw_md.s), added with Micronut99's approval
# (Oct 2026: test the Genesis-plane idea in REV). These exact blocks must be present; they are
# taken out before the md5 comparison, so the rest of each file must still be Hurrican's byte
# for byte. Nothing in the boot sequence calls them (docs/REV2_NOTES.md).
REV2_ADDITIONS = {
    'cd32x/subcpu/hw_md.s': [
        (b'        .equ    MD_CMD_END, 38              /* KOBO K19: + set_vdp_reg (34); D32X M1: + start_d32xr (35); REV2: + set_vsram (36); REV4: + copy_vram_col (37) */',
         b'        .equ    MD_CMD_END, 36              /* KOBO K19: + set_vdp_reg (34); D32X M1: + start_d32xr (35) */'),
        (b"        .word   start_d32xr - cmd_table         /* 35  D32X M1 */\n        .word   set_vsram - cmd_table           /* 36  SuperTux REV2 */\n        .word   copy_vram_col - cmd_table       /* 37  SuperTux REV4 */\n\n| SuperTux REV2: void set_vsram(int addr, int word);\n| One word into the VDP's vertical scroll RAM (VSRAM) at byte address addr (0 = plane A,\n| 2 = plane B in full-screen mode; up to 0x4E in 2-cell column mode).  WHY a command of its\n| own: VSRAM is not video RAM - set_vram's address command always targets VRAM (0x4000....),\n| a VSRAM write needs the code 0x40000010.  Same shape as set_vram: auto-increment 2, the\n| 32-bit address command in ONE move.l (an interrupt cannot split it), then the data word.\n| Runs in the command loop, never inside an interrupt.\nset_vsram:\n        lea     0xC00000,a1\n        move.w  #0x8F02,4(a1)\n        move.l  4(sp),d1\n        andi.l  #0x0000007E,d1\n        swap    d1\n        ori.l   #0x40000010,d1\n        move.l  d1,4(a1)\n        move.l  8(sp),d0\n        move.w  d0,(a1)\n        moveq   #0,d0\n        rts\n\n| SuperTux REV4: void copy_vram_col(int addr, short *src, int words);\n| copy_vram with the VDP's auto-increment at 128 bytes instead of 2: the words go DOWN one\n| column of a 64-cell-wide plane map (one map row is 64 words).  WHY: a plane map is stored row\n| by row, so a new column scrolling in is 28-30 words 128 bytes apart - with copy_vram that would\n| be one command a word.  The increment is put back to 2 afterwards (every other routine sets it\n| itself too).  Runs in the command loop, never inside an interrupt.\ncopy_vram_col:\n        lea     0xC00000,a1\n        move.w  #0x8F80,4(a1)\n        move.l  4(sp),d1\n        lsl.l   #2,d1\n        lsr.w   #2,d1\n        swap    d1\n        ori.l   #0x40000000,d1\n        move.l  d1,4(a1)\n        movea.l 8(sp),a0\n        move.l  12(sp),d0\n        subq.w  #1,d0\n0:\n        move.w  (a0)+,(a1)\n        dbra    d0,0b\n        move.w  #0x8F02,4(a1)\n        moveq   #0,d0\n        rts\n",
         b'        .word   start_d32xr - cmd_table         /* 35  D32X M1 */\n'),
    ],
    'cd32x/subcpu/main.c': [
        (b"#define MD_CMD_START_D32XR    35         /* D32X M1: 68K -> D32XR's _start (never returns) */\n#define MD_CMD_SET_VRAM        5         /* hw_md.s set_vram(addr, word) - Chilly's, unused until REV2 */\n#define MD_CMD_COPY_VRAM       7         /* hw_md.s copy_vram(addr, src, words) */\n#define MD_CMD_SET_PALETTE    12         /* hw_md.s set_palette(src, index, count) */\n#define MD_CMD_SET_VSRAM      36         /* SuperTux REV2: hw_md.s set_vsram(addr, word) */\n#define MD_CMD_COPY_VRAM_COL  37         /* SuperTux REV4: hw_md.s copy_vram_col(addr, src, words) */\n",
         b"#define MD_CMD_START_D32XR    35         /* D32X M1: 68K -> D32XR's _start (never returns) */\n"),
        (b"        /* ---------------------------------------------------------------------------------\n           SuperTux REV2 - THE GENESIS PICTURE FOR THE SH2 (commands 62-66).\n           The 68000 listener could always write the Genesis VDP (Chilly's set_vram /\n           copy_vram / set_palette, Kobo's set_vdp_reg) but no SH2 command reached them: the\n           SH2 could only switch the Genesis text on / off (52).  These five let the game use\n           the Genesis planes - tiles and maps uploaded from the RAM cart, scrolled by\n           register.  They run only when the SH2 sends them, i.e. after the game has started:\n           nothing in the boot sequence calls them.  Parameters as the other commands: COMM2\n           (16 bits) and COMM8 (32 bits: COMM8 and COMM10), written by the SH2 BEFORE COMM0;\n           done = COMM0 cleared.  Cart sources are read by the 68000 through the 32X bank\n           window 0x900000 (RV 0 - as CMD 46's read-back), so the SH2 is never locked out.\n             62  VRAM word     COMM2 = VRAM byte address, COMM8 (high 16) = the word\n             63  cart -> VRAM  COMM2 = VRAM byte address, COMM8L = words << 22 | cart offset\n                               (even, < 4 MB; 1..1023 words); reply COMM12 = 1 ok, 0 refused\n             64  cart -> CRAM  COMM2 = first colour (0-63), COMM8L = count << 22 | cart offset\n                               (1..64 colours); reply COMM12 = 1 ok, 0 refused\n             65  VDP register  COMM2 = 0x8000 | reg << 8 | value (registers 0-23 only)\n             66  VSRAM word    COMM2 = VSRAM byte address (0 plane A, 2 plane B), COMM8 (high\n                               16) = the vertical scroll\n           A copy that crosses a 1 MB bank of the window is done in two parts. */\n        else if (cmd == 62 || cmd == 66) {\n            int addr = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122) & 0xFFFF;\n            int word = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15128) & 0xFFFF;\n            if (cmd == 62) do_md_cmd2(MD_CMD_SET_VRAM, addr & 0xFFFE, word);\n            else do_md_cmd2(MD_CMD_SET_VSRAM, addr & 0x7E, word);\n            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);\n        }\n        else if (cmd == 63 || cmd == 64) {\n            int dst = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122) & 0xFFFF;\n            uint32_t p = (uint32_t)do_md_cmd1(MD_CMD_READ_32X_32, 0xA15128);\n            uint32_t coff = p & 0x3FFFFF;\n            int n = (int)(p >> 22);\n            int ok = n > 0 && !(coff & 1) && (cmd == 63 ? !(dst & 1) : (dst < 64 && dst + n <= 64));\n            while (ok && n > 0) {\n                /* the part inside this 1 MB bank of the window */\n                int fit = (int)((0x100000 - (coff & 0xFFFFF)) >> 1);\n                int k = n < fit ? n : fit;\n                do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15104, (int)(coff >> 20) & 3);\n                if (cmd == 63) do_md_cmd3(MD_CMD_COPY_VRAM, dst, 0x900000 + (coff & 0xFFFFF), k);\n                else do_md_cmd3(MD_CMD_SET_PALETTE, 0x900000 + (coff & 0xFFFFF), dst, k);\n                n -= k;\n                coff += (uint32_t)k * 2;\n                dst += cmd == 63 ? k * 2 : k;\n            }\n            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512C, ok);\n            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);\n        }\n        /* SuperTux REV4 - 67  cart -> VRAM in COLUMNS (a plane map 64 cells wide, one map row =\n           128 bytes): COMM2 = VRAM byte address of the first column's top word, COMM8L =\n           (columns - 1) << 30 | words a column << 22 | cart offset (even; 1-4 columns of 1-255\n           words, stored one column after the other).  Column k goes to address + 2 k.  Reply\n           COMM12 = 1 ok / 0 refused.  WHY: a plane scrolling sideways needs a new map column\n           every 8 pixels - 30 words 128 bytes apart (cmd 63 copies rows). */\n        else if (cmd == 67) {\n            int dst = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122) & 0xFFFF;\n            uint32_t p = (uint32_t)do_md_cmd1(MD_CMD_READ_32X_32, 0xA15128);\n            uint32_t coff = p & 0x3FFFFF;\n            int rows = (int)((p >> 22) & 0xFF), cols = (int)(p >> 30) + 1, k;\n            int ok = rows > 0 && !(coff & 1) && !(dst & 1)\n                     && ((coff & 0xFFFFF) + (uint32_t)(rows * cols * 2) <= 0x100000);   /* one bank */\n            if (ok) {\n                do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15104, (int)(coff >> 20) & 3);\n                for (k = 0; k < cols; k++)\n                    do_md_cmd3(MD_CMD_COPY_VRAM_COL, dst + 2 * k, 0x900000 + (coff & 0xFFFFF) + (uint32_t)(k * rows * 2), rows);\n            }\n            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512C, ok);\n            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);\n        }\n        else if (cmd == 65) {\n            int w = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122) & 0xFFFF;\n            if ((w & 0xE000) == 0x8000 && ((w >> 8) & 0x1F) <= 23) do_md_cmd1(MD_CMD_SET_VDP_REG, w);\n            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);\n        }\n        else if (cmd == 48) {\n            /* (K5: normally handled by the fast path",
         b'        else if (cmd == 48) {\n            /* (K5: normally handled by the fast path'),
    ],
}

bad = 0
switched = 0
added = 0
listed = 0
for line in open(os.path.join(HERE, "docs/BOOT_FILES.md5")):
    if line.startswith("#") or not line.strip():
        continue
    want, name = line.split(None, 1)
    name = name.strip()
    listed += 1
    p = os.path.join(HERE, name)
    if not os.path.exists(p):
        print("MISSING  %s" % name)
        bad += 1
        continue
    data = open(p, "rb").read()
    for here, orig in REV2_ADDITIONS.get(name, []):
        if data.count(here) == 1:
            data = data.replace(here, orig)
            added += 1
        else:
            print("REV2     %s: a REV2 command block is missing or changed" % name)
            bad += 1
    for here, orig in SWITCHES.get(name, []):
        if data.count(here) == 1:
            data = data.replace(here, orig)
            switched += 1
        elif data.count(orig) != 1:
            print("SWITCH   %s: neither the release nor the diagnostics line is there exactly once" % name)
            bad += 1
    if hashlib.md5(data).hexdigest() != want:
        print("CHANGED  %s" % name)
        bad += 1
if bad:
    print("*** the boot path differs from Hurrican CD32X's in %d place(s) - stopping" % bad)
    sys.exit(1)
state = "release: no diagnostics text, the splash only" if switched == 0 else "%d of 4 diagnostics switches on" % switched
print("boot path: %d files identical to Hurrican CD32X's (%s) + %d REV2 command blocks" % (listed, state, added))
