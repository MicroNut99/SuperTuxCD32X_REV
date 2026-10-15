#!/usr/bin/env python3
"""check_sdram_code.py - stops the build if code placed in .data / .sdcode (copied to SDRAM)
contains alignment padding that the CPU would run into.

    python3 tools/check_sdram_code.py mars/stx32x.elf [objdump]

Why (R1c on the console: "SH2 EXCEPTION - ILLEGAL INSTRUCTION, PC 06000A26" in Vic's
draw_handle_layercmd): YATSSD puts its drawing functions in the .data section
(ATTR_DATA_ALIGNED, so they run from SDRAM). In a .data section the assembler pads an
".align" with zero bytes, not with NOPs (0x0009) as in .text. Built with -O2, gcc aligns loop
starts and jump targets inside functions - and the CPU falls through into the 0x0000 padding:
an illegal instruction. Vic builds with -fno-align-loops -fno-align-jumps -fno-align-labels
(yatssd/Makefile "release"), which mars/Makefile now uses for his files too.

The check: in every function in an SDRAM section, a 0x0000 word that the CPU can fall into
(the instruction before it is not an unconditional branch / return, or that branch's delay
slot) is reported. Literal pools after a branch are not."""
import re
import subprocess
import sys

elf = sys.argv[1]
objdump = sys.argv[2] if len(sys.argv) > 2 else "sh-elf-objdump"
out = subprocess.run([objdump, "-d", "-j", ".data", "-j", ".sdcode", elf],
                     capture_output=True, text=True, check=True).stdout
# only C functions (symbol type FUNC); crt0's vector tables and variables are data
syms = subprocess.run([objdump, "-t", elf], capture_output=True, text=True, check=True).stdout
funcs = {l.split()[-1] for l in syms.splitlines() if " F " in l and (".data" in l or ".sdcode" in l)}
# pass 1: every function's halfwords, and the literal-pool words its PC-relative loads read
code, pool = {}, set()
func = None
for line in out.splitlines():
    m = re.match(r"^([0-9a-f]+) <(.+)>:$", line)
    if m:
        func = m.group(2) if m.group(2) in funcs else None
        if func:
            code[func] = []
        continue
    m = re.match(r"^\s*([0-9a-f]+):\s+([0-9a-f]{2}) ([0-9a-f]{2})\s+(.*)$", line)
    if not m or func is None:
        continue
    addr = int(m.group(1), 16)
    code[func].append((addr, m.group(2) + m.group(3), m.group(4).strip()))
    p_ = re.match(r"^mov\.(l|w)\s+([0-9a-f]+) <", m.group(4).strip())
    if p_:
        t = int(p_.group(2), 16)
        pool.update((t, t + 2) if p_.group(1) == "l" else (t,))
# pass 2: a 0x0000 that is not pool data and has more code after it in the same function is
# padding the CPU can run into (the padding after a function's last word is harmless)
bad = []
for f, ws in code.items():
    last_code = max((a for a, h, i in ws if h != "0000" and a not in pool), default=0)
    for a, h, i in ws:
        if h == "0000" and a not in pool and a < last_code:
            bad.append("%08x in %s" % (a, f))
if bad:
    print("*** code in SDRAM falls into 0x0000 padding (an illegal instruction on the SH2):")
    for b in bad:
        print("    " + b)
    sys.exit(1)
print("SDRAM code: no padding the CPU can fall into")
