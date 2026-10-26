#!/bin/bash
# SuperTux CD32X REV - builds the REV10b disc: the whole of SuperTux 0.1.3 (Icyisland's 26 levels,
# both Bonus Islands, the title with its demo, world maps, bad guys, power-ups, saves), half size,
# sharpened. REV is the rip and rebuild of R11's drawing (README.md). REV2 adds the Genesis
# picture commands to the Sub-CPU / 68000 programs (tools/check_boot.py knows the exact blocks)
# and has the green and white boot text on (KOBO_DIAG_TEXT 1). REV3 uses them: OPTIONS -
# BACKGROUND GENESIS puts a level's background picture on the Genesis' plane B
# (tools/stx_genesis.py, docs/REV3_NOTES.md). REV4: OPTIONS - DRAW GENESIS puts the whole level
# on the Genesis' planes, the 32X draws only what moves (game/gen_level.c, docs/REV4_NOTES.md);
# it adds command 67 (columns into VRAM) to the Sub-CPU / 68000 programs - test it in Fusion first.
# REV5: every level's GENLVL.BIN carries its own plane B (no picture pack needed), DG / DX on the
# timing line, saves in the RAM cart's save area (piece 13) - docs/REV5_NOTES.md.
# REV6: the game is built from Genesis tiles - every level and the demo; no 32X version of the
# levels, no Draw / Background switch (docs/REV6_NOTES.md). REV7: the 32X in front of the Genesis
# during levels (docs/REV7_NOTES.md). REV8: the sky is a 32X gradient behind the Genesis level
# (docs/REV8_NOTES.md). REV9: the sky dithered (no bands), 4 palettes for the level
# (docs/REV9_NOTES.md). REV10: big tile pictures (the exit igloo) drawn whole, slower credits,
# small Tux in the title demo (docs/REV10_NOTES.md).
#
# The steps are Hurrican CD32X's build (= the OpenJazz port's J19b, the last disc proven on the
# console), with SuperTux's data in place of Hurrican's:
#   boot path check -> data -> splash -> packs + sound effects -> music (CD audio tracks 2-13)
#   -> the PC check -> program (mars/, with J19b's own switches) -> ROM check (OpenLara's
#   prep_openlara_cd.py) -> SH2 loader -> Sub-CPU / 68000 -> disc image with the audio tracks.
# The boot path is Hurrican's, unchanged (tools/check_boot.py stops the build otherwise):
# cd32x/ (loader, Sub-CPU, 68000 listener), port/ (start-up, platform layer),
# tools/make_oj_rom.py, tools/make_boot_image.py, tools/mars_boot.bin. The program goes on the
# disc as OPENLARA.32X (file 20) with the J19b engine's module name.
#
#   ./build.sh 2>&1 | tee build_log.txt
# SuperTux 0.1.3 is read from SuperTux-0.1.3/ next to this file (tools/stx_fetch.sh gets it)
# or from SUPERTUX=<folder>. DATA_ONLY=1 stops after the data (no SH2 / m68k toolchain needed).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
trap 'echo; echo "*** BUILD FAILED at line $LINENO: $BASH_COMMAND"; echo "*** look above for the first error (grep -n -B6 rror build_log.txt)"' ERR
SRC="${SUPERTUX:-$HERE/SuperTux-0.1.3}"
OUT="$HERE/build"
ROMS="$HERE/cd32x/subcpu/ROMS"
MARKER="SH2-OPENLARA-CD-OL1-LOADER"
# 8 / 16 MB TEST BUILDS (16MB-PCB-CD32X, docs/CART_8MB_16MB_DESIGN.md): CART_MB=8 or 16 ./build.sh
#   - the Sub-CPU program gets command 68 (window 1's page register; docs repo
#     CD32X_CART_PAGING_PROPOSAL.md) - compiled ONLY in these builds (STX_CART_PAGED);
#   - the game probes the cart (4 / 8 / 16 MB, shown on the title), keeps levels in pages 2-7,
#     trusts a level already in the cart only by its sum (SUMS.BIN);
#   - 16 MB: every level is copied into the cart on the first start (a loading screen once),
#     after that no loading screens; the cart keeps them (battery) and each is checked by its sum.
#   - the name (title, disc, zip, git tag) is game/stx.h's STX_VERSION for the cart size.
# ED: CART_MB=ED - the Mega EverDrive PRO as the 16 MB paged cart (CD32X BootCamp docs/12): the
#   listener's MODE1-9 turns the page writes into the EverDrive's window registers; pages 1-6 (page 7
#   would hold its bank 31, the OS's), the levels kept in pages 2-6 the way the 8 MB build keeps them
#   in 2-3 (no first-start copy of every level: 77 do not fit six pages).  EDSAVE-4: both SH2s wait
#   in SDRAM for every command whose 68000 side holds RV = 1.
#   CART_MB=ED EDCD=1 ./build.sh    the disc + cd32x/subcpu/EDCD.32X, the launcher started from the
#                                   EverDrive menu with the disc in the Sega CD (BOOTCAMP=<folder>,
#                                   default ../CD32X-BootCamp)
# Saves still go to the cart's page 1 (the EverDrive wipes its PSRAM at its menu: nothing persists yet).
CART_MB=${CART_MB:-4}
case "$CART_MB" in
  4)    PACK_ARGS=""; ENGINE_DEFS=""; SUB_HWFLAGS="" ;;
  8|16) PACK_ARGS="--cart-mb $CART_MB"; ENGINE_DEFS="-DSTX_CART_MB=$CART_MB"
        # main.c is compiled with the Makefile's HWFLAGS (not CCFLAGS): the same flags + the switch.
        # (The first test discs set CCFLAGS - command 68 was never in the Sub-CPU program and the
        # SH2 sat out its wait for an answer: the ~2 minute stop after the loading bar.)
        SUB_HWFLAGS="-m68000 -Wall -O1 -c -fomit-frame-pointer -DSTX_CART_PAGED=1"
        ;;
  ED)   PACK_ARGS="--cart-mb 16 --cart-ed"; ENGINE_DEFS="-DSTX_CART_MB=16 -DSTX_CART_ED"
        SUB_HWFLAGS="-m68000 -Wall -O1 -c -fomit-frame-pointer -DSTX_CART_PAGED=1 -DCART_ED"
        echo "==> cart: the Mega EverDrive PRO (16 MB, pages 1-6)"
        ;;
  *)    echo "*** CART_MB is 4, 8, 16 or ED - stopping"; exit 1 ;;
esac
# the build's name: game/stx.h STX_VERSION (REV11-T2 / REV11-8MB-T1 / REV11-16MB-T1 / REV11-ED-T1) -
# the disc, the title, the zip and the git tag carry the same
VERSION=$(sed -n "s/^#define STX_VERSION_${CART_MB}MB *\"\(.*\)\"/\1/p" "$HERE/game/stx.h")
[ -n "$VERSION" ] || { echo "*** no STX_VERSION_${CART_MB}MB in game/stx.h - stopping"; exit 1; }
DISC=SuperTux$VERSION
mkdir -p "$OUT" "$ROMS"

echo "==> boot path check (must be Hurrican CD32X's, unchanged)"
python3 "$HERE/tools/check_boot.py"
python3 -c "import numpy, PIL" 2>/dev/null \
  || { echo "*** Python needs numpy and Pillow: sudo apt install python3-numpy python3-pil - stopping"; exit 1; }
grep -q '^#define CDL_TARGET_OPENLARA 1' "$HERE/cd32x/sh2/cdloader_sh2.c" \
  || { echo "*** cd32x/sh2/cdloader_sh2.c is not in OpenLara mode - stopping"; exit 1; }
[ -f "$SRC/data/images/tilesets/supertux.stgt" ] || "$HERE/tools/stx_fetch.sh" "$SRC"

echo "==> data (tiles, palette, sprites, backgrounds, levels) and previews"
rm -rf "$OUT/data" "$OUT/previews"
python3 "$HERE/tools/stx_convert.py" "$SRC" "$OUT/data" --previews "$OUT/previews"

echo "==> splash";      python3 "$HERE/tools/make_stx_splash.py" "$SRC/data" "$OUT/Splash.png"
                        python3 "$HERE/tools/make_boot_image.py" "$OUT/Splash.png" "$ROMS/IMAGE.RAW"
                        python3 "$HERE/tools/splash_bar_colours.py" "$ROMS/IMAGE.RAW"
echo "==> data packs";  rm -f "$ROMS"/IMAGE8.RAW "$ROMS"/IMAGE9.RAW "$ROMS"/OJ*.PAK "$ROMS"/SFX*.BIN
                        python3 "$HERE/tools/stx_pack.py" $PACK_ARGS "$OUT/data" "$ROMS"
# (stx_pack.py itself stops if IMAGE8.RAW would run into the level areas)
echo "==> sound effects"; python3 "$HERE/tools/make_stx_sfx.py" "$SRC/data" "$ROMS/SFX00.BIN"
echo "==> music (CD audio tracks 2-13)"
                        "$HERE/tools/stx_music.sh" "$SRC" "$OUT/music"

echo "==> PC check (game/ on the PC: level 1, 300 frames, frames in build/host_frames)"
                        make -B -C "$HERE/host" EXTRA="$ENGINE_DEFS" > /dev/null
                        rm -rf "$OUT/host_frames"; mkdir -p "$OUT/host_frames"
                        ST_CARTMB=$CART_MB ST_LEVEL=0 ST_DIR="$ROMS" ST_OUT="$OUT/host_frames" ST_FRAMES=300 ST_SAVE_EVERY=30 \
                          ST_PAD="0:-,10:RIGHT,60:RIGHT+C,80:RIGHT,140:RIGHT+B,170:RIGHT+B+C,190:RIGHT+B" "$HERE/host/stx_host"
[ "${DATA_ONLY:-0}" = 1 ] && { echo "==> DATA_ONLY=1: stopping before the console build"; exit 0; }

echo "==> engine";      rm -rf "$HERE/mars/obj"     # the switches are compiled into port/plat_mars.c
                        make -C "$HERE/mars" EXTRA_DEFS="-DOJ32X_MUSIC=1 $ENGINE_DEFS"
[ -f "$HERE/mars/OPENJAZZ.32X" ] || { echo "*** mars/OPENJAZZ.32X was not made - stopping"; exit 1; }
PREP_MODE1=""; if [ "${EDCD:-0}" = 1 ]; then PREP_MODE1="--mode1"; fi   # MODE1-1 (EDCD=1, below): the RAM jump table too
echo "==> ROM check";   python3 "$HERE/cd32x/ref/openlara/tools/prep_openlara_cd.py" $PREP_MODE1 "$HERE/mars/OPENJAZZ.32X" "$ROMS/OPENLARA.32X"
echo "==> SH2 loader";  cd "$HERE/cd32x/sh2"    && make -f Makefile.cdloader clean && make -f Makefile.cdloader
echo "==> Sub-CPU";     cd "$HERE/cd32x/subcpu" && make clean
if [ -n "$SUB_HWFLAGS" ]; then
    make cd HWFLAGS="$SUB_HWFLAGS" 2>&1 | tee "$HERE/cd32x/subcpu/make_sub.log"
    # command 68 must really be compiled in: main.c's compile line carries the switch
    grep -q "STX_CART_PAGED=1.* main\.c -o main\.o" "$HERE/cd32x/subcpu/make_sub.log" \
        || { echo "*** main.c was not compiled with STX_CART_PAGED - no command 68 - stopping"; exit 1; }
    echo "    Sub-CPU: main.c compiled with command 68"
else
    make cd
fi

ISO="$HERE/cd32x/subcpu/CDROMPlayer.iso"
[ -f "$ISO" ] || { echo "*** $ISO was not made - stopping"; exit 1; }
grep -a -q "$MARKER" "$ISO" || { echo "*** the disc does NOT contain the loader - stopping"; exit 1; }
for f in APP.BIN CDLOADER.BIN OPENLARA.32X IMAGE.RAW IMAGE8.RAW SFX00.BIN; do
  [ -f "$HERE/cd32x/subcpu/cd/$f" ] || { echo "*** $f is not on the disc - stopping"; exit 1; }
done
# every level file tools/stx_pack.py made (IMAGE9.RAW: area A; OJnnn.PAK: area B - PACKS.TXT)
while read -r f; do
  [ -z "$f" ] && continue
  [ -f "$HERE/cd32x/subcpu/cd/$f" ] || { echo "*** $f is not on the disc - stopping"; exit 1; }
  cmp -s "$HERE/cd32x/subcpu/cd/$f" "$ROMS/$f" || { echo "*** the disc has an old $f - stopping"; exit 1; }
done < "$ROMS/PACKS.TXT"
NAME="$(dd if="$HERE/cd32x/subcpu/cd/OPENLARA.32X" bs=1 skip=$((0x3C0)) count=8 2>/dev/null)"
[ "$NAME" = "OpenJazz" ] || { echo "*** cd/OPENLARA.32X does not carry the J19b module name ($NAME) - stopping"; exit 1; }
cmp -s "$HERE/cd32x/subcpu/cd/OPENLARA.32X" "$ROMS/OPENLARA.32X" || { echo "*** the disc has an old program - stopping"; exit 1; }
echo "==> disc checked: loader + program + splash + packs + sound effects"

# the mixed-mode image: data track + audio tracks 2-13
cd "$HERE/cd32x/subcpu" && python3 make_mixed_cd2.py CDROMPlayer.iso "$OUT"/music/Track*.wav --out $DISC
echo
echo "==> done:"
echo "    disc: cd32x/subcpu/$DISC.cue (with its .bin) - for a burner (Fusion cannot emulate the RAM cart)"
echo "    build/host_frames/         what the PC check drew (PPM)"
echo "    build/previews/            every level as the 32X will draw it"

# EDCD=1 (MODE1-1, CD32X BootCamp docs/12): EDCD.32X, made by the BootCamp's edboot/build_edcd.sh around
# this disc's Sub-CPU program built a second time for the cartridge boot (MODE1=1 M1PAGE=1, in a copy
# of cd32x/subcpu: the disc keeps the normal program)
if [ "${EDCD:-0}" = 1 ]; then
  BC="${BOOTCAMP:-$HERE/../CD32X-BootCamp}"
  [ -f "$BC/edboot/build_edcd.sh" ] || { echo "*** EDCD=1 needs the CD32X BootCamp next to this folder (or BOOTCAMP=<folder>): not at $BC"; exit 1; }
  echo "==> EDCD.32X: the disc started from the EverDrive PRO (cartridge boot)"
  T=$(mktemp -d); mkdir -p "$T/subcpu/ROMS" "$T/sh2"
  find "$HERE/cd32x/subcpu" -maxdepth 1 -type f -exec cp {} "$T/subcpu/" \;
  cp "$HERE/cd32x/sh2/CDLOADER.32x" "$T/sh2/"
  ( cd "$T/subcpu" && make clean > /dev/null && make CDROMPlayer.bin MODE1=1 M1CTRL0=0x8000 M1PAGE=1 ${SUB_HWFLAGS:+HWFLAGS="$SUB_HWFLAGS"} > "$T/mode1.log" 2>&1 ) \
    || { tail -20 "$T/mode1.log"; echo "*** the MODE1 Sub-CPU program did not build"; exit 1; }
  bash "$BC/edboot/build_edcd.sh" "$T/subcpu/CDROMPlayer.bin" "$HERE/cd32x/sh2/CDLOADER.32x" "$HERE/cd32x/subcpu/EDCD.32X"
  rm -rf "$T"
  echo "==> EDCD.32X: $HERE/cd32x/subcpu/EDCD.32X - copy it to the EverDrive's SD card and start it from the menu"
  echo "    with the disc above in the Sega CD (not \"Run Mega-CD\")"
fi
