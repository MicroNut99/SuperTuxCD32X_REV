#!/bin/bash
# SuperTux CD32X REV - builds the REV6 disc: the whole of SuperTux 0.1.3 (Icyisland's 26 levels,
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
# levels, no Draw / Background switch (docs/REV6_NOTES.md).
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
DISC=SuperTuxREV6
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
                        python3 "$HERE/tools/stx_pack.py" "$OUT/data" "$ROMS"
# (stx_pack.py itself stops if IMAGE8.RAW would run into the level areas)
echo "==> sound effects"; python3 "$HERE/tools/make_stx_sfx.py" "$SRC/data" "$ROMS/SFX00.BIN"
echo "==> music (CD audio tracks 2-13)"
                        "$HERE/tools/stx_music.sh" "$SRC" "$OUT/music"

echo "==> PC check (game/ on the PC: level 1, 300 frames, frames in build/host_frames)"
                        make -C "$HERE/host" > /dev/null
                        rm -rf "$OUT/host_frames"; mkdir -p "$OUT/host_frames"
                        ST_LEVEL=0 ST_DIR="$ROMS" ST_OUT="$OUT/host_frames" ST_FRAMES=300 ST_SAVE_EVERY=30 \
                          ST_PAD="0:-,10:RIGHT,60:RIGHT+C,80:RIGHT,140:RIGHT+B,170:RIGHT+B+C,190:RIGHT+B" "$HERE/host/stx_host"
[ "${DATA_ONLY:-0}" = 1 ] && { echo "==> DATA_ONLY=1: stopping before the console build"; exit 0; }

echo "==> engine";      rm -rf "$HERE/mars/obj"     # the switches are compiled into port/plat_mars.c
                        make -C "$HERE/mars" EXTRA_DEFS="-DOJ32X_MUSIC=1"
[ -f "$HERE/mars/OPENJAZZ.32X" ] || { echo "*** mars/OPENJAZZ.32X was not made - stopping"; exit 1; }
echo "==> ROM check";   python3 "$HERE/cd32x/ref/openlara/tools/prep_openlara_cd.py" "$HERE/mars/OPENJAZZ.32X" "$ROMS/OPENLARA.32X"
echo "==> SH2 loader";  cd "$HERE/cd32x/sh2"    && make -f Makefile.cdloader clean && make -f Makefile.cdloader
echo "==> Sub-CPU";     cd "$HERE/cd32x/subcpu" && make clean && make cd

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
