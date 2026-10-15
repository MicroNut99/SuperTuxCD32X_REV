#!/bin/bash
# where.sh - names the functions behind the addresses on the red "SH2 EXCEPTION" screen
# (the screen says: TOOLS/WHERE.SH <ADDRESS>). Run it on the PC that built the disc, in this
# folder, with the addresses from the screen (PC, PR and the CALLS):
#     tools/where.sh 0202000A 0201FFF8 020240FA ...
# It reads mars/stx32x.elf - the program of the LAST build, so the screen must come from a disc
# made by that build. The HEAP line on the screen tells builds apart (its "OF" number is the
# free SDRAM, which changes with the program): R2 shows 92080 here, R3 about 45800.
TC="${TC:-/opt/toolchains/sega/sh-elf/bin/sh-elf-}"
ELF="$(dirname "$0")/../mars/stx32x.elf"
[ -f "$ELF" ] || { echo "no $ELF - build first"; exit 1; }
for a in "$@"; do
  printf "%s  " "$a"
  "${TC}addr2line" -f -e "$ELF" "0x${a#0x}" | head -1
done
