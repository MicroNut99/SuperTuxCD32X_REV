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

bad = 0
switched = 0
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
print("boot path: %d files identical to Hurrican CD32X's (%s)" % (listed, state))
