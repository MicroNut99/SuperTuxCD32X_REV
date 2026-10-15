#!/bin/bash
# SuperTux's modules -> CD audio tracks (WAV, 44.1 kHz stereo), rendered with the bundled
# libxmp-lite (tools/it2wav.c, from the Hurrican port). One pass each: the console repeats a
# track. Each track is then brought to a peak of 0.70 (tools/normalize_wav.py: SuperTux's
# modules render quietly - R1c played faintly on the console); the sound effects stay louder.
#   tools/stx_music.sh <SuperTux 0.1.3 folder> <out folder>
# The order is tools/stx_convert.py's MUSIC list: track 2 = theme, 3 = Mortimers_chipdisko ...
set -e
SRC="$1"; OUT="$2"; HERE="$(cd "$(dirname "$0")" && pwd)"
[ -d "$SRC/data/music" ] || { echo "*** no SuperTux music in $SRC/data/music"; exit 1; }
mkdir -p "$OUT"
gcc -O2 -DLIBXMP_CORE_PLAYER -DLIBXMP_STATIC -I"$HERE/libxmp-lite/include" -I"$HERE/libxmp-lite/src" \
    -o "$OUT/it2wav" "$HERE/it2wav.c" "$HERE"/libxmp-lite/src/*.c "$HERE"/libxmp-lite/src/loaders/*.c -lm
T=2
for SONG in theme Mortimers_chipdisko Mortimers_chipdisko-fast cave cave-fast fortress fortress-fast \
            fortress2 fortress2-fast SALCON SALCON-fast leveldone; do
  F=""
  for E in mod MOD; do [ -f "$SRC/data/music/$SONG.$E" ] && F="$SRC/data/music/$SONG.$E"; done
  [ -n "$F" ] || { echo "*** $SONG.mod missing"; exit 1; }
  "$OUT/it2wav" "$F" "$OUT/Track$(printf %02d $T).wav" 300 1 0.32 >/dev/null
  G="$(python3 "$HERE/normalize_wav.py" "$OUT/Track$(printf %02d $T).wav" 0.70)"   # R1c: faint on the console
  printf "    track %2d  %-26s %s  (%s)\n" $T "$SONG" "$(python3 -c "import wave,sys; w=wave.open(sys.argv[1]); print('%.1f s' % (w.getnframes() / 44100))" "$OUT/Track$(printf %02d $T).wav")" "$G"
  T=$((T + 1))
done
