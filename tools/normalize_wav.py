#!/usr/bin/env python3
"""normalize_wav.py - scales a 16-bit WAV so its loudest sample is at PEAK (default 0.70).

    python3 tools/normalize_wav.py Track03.wav [peak]

SuperTux's modules render quietly (R1c on the console: "the music is playing faintly" - peaks
0.16-0.22, about -15 dB): each CD track is brought to the same peak, which leaves the
sound effects (normalised to 0.9 on the PCM chip) still above the music."""
import array
import sys
import wave

path = sys.argv[1]
peak = float(sys.argv[2]) if len(sys.argv) > 2 else 0.70
w = wave.open(path, "rb")
params = w.getparams()
a = array.array("h", w.readframes(params.nframes))
w.close()
if sys.byteorder == "big":
    a.byteswap()
top = max(1, max(abs(v) for v in a))
g = peak * 32767 / top
a = array.array("h", (max(-32768, min(32767, int(v * g))) for v in a))
if sys.byteorder == "big":
    a.byteswap()
w = wave.open(path, "wb")
w.setparams(params)
w.writeframes(a.tobytes())
w.close()
print("gain %.2f" % g, end="")
