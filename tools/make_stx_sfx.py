#!/usr/bin/env python3
"""make_stx_sfx.py - SuperTux 0.1.3's sound effects -> the Sega CD PCM pack SFX00.BIN
(Hurrican CD32X's tools/make_hur_sfx.py with SuperTux's sounds; the pack format is unchanged)

    python3 tools/make_stx_sfx.py <SuperTux 0.1.3 folder>/data cd32x/subcpu/ROMS/SFX00.BIN

The same pack as the OpenJazz port's (tools/make_oj_sfx.py there, J19b: KSFX, loaded at start-up
with command 53 into the PCM chip's 64 KB), so the disc has the same files at boot as the
console-proven Jazz disc. The game plays effect n with plat_sfx(n): SOUNDS[n - 1] below.
    0x000 'KSFX' u16 count u16 image_bytes (0 = 65536)
    0x008 count x (u8 start_page, u8 volume, u16 fd, u16 loop_address, u16 length)
    0x100 PCM image (RF5C164: 8-bit sign-magnitude, 0xFF = loop marker)
    after the image: u8 map[256] - sample index -> pack effect (0 = not in the pack)"""
import os
import struct
import sys

RATE, MAX_SECS, RAM, PAGE, TAIL = 5000, 0.8, 65536, 256, 16
# the game's sound numbers (game/stx.h SFX_*): plat_sfx(n) plays SOUNDS[n - 1]
# (the Sub-CPU looks up map[n - 1], cd32x/subcpu/main.c OL-6)
SOUNDS = ["jump.wav", "skid.wav", "distro.wav", "lifeup.wav", "hurt.wav", "bigjump.wav",
          "stomp.wav", "squish.wav", "kick.wav", "brick.wav", "bump-upgrade.wav", "fall.wav",
          "excellent.wav", "coffee.wav", "herring.wav", "upgrade.wav", "shoot.wav", "explode.wav",
          "ricochet.wav", "warp.wav"]
# each sound is normalised to the same peak (as Hurrican's); the music is rendered 10 dB lower
# (tools/stx_music.sh) so the sound effects are heard over it
VOLUME, PEAK = 140, 0.9


def sm8(v):
    m = min(126, int(round(abs(v) * 127)))       # 127 would give the 0xFF loop marker
    return (0x80 | m) if v >= 0 else m


def lowpass(a, src, dst):
    w = max(1, int(round(src / dst)))
    for _ in range(2):
        out, acc = [], 0.0
        for i, v in enumerate(a):
            acc += v
            if i >= w:
                acc -= a[i - w]
            out.append(acc / min(i + 1, w))
        a = out
    return a


def resample(a, src, dst):
    n = max(1, int(round(len(a) * dst / src)))
    out = []
    for i in range(n):
        x = i * (len(a) - 1) / max(1, n - 1)
        j = int(x)
        f = x - j
        out.append(a[j] * (1 - f) + a[min(j + 1, len(a) - 1)] * f)
    return out


def read_wav(path):
    """mono samples -1..1 and the rate; None if the file is not plain PCM.
    Reads the RIFF chunks itself: some of SuperTux's files (jump.wav, bigjump.wav, squish.wav)
    have a wrong RIFF length, and Python's wave module then returns no samples."""
    try:
        d = open(path, "rb").read()
    except OSError:
        return None
    if d[:4] != b"RIFF" or d[8:12] != b"WAVE":
        return None
    fmt, raw, i = None, None, 12
    while i + 8 <= len(d):
        cid, sz = d[i:i + 4], struct.unpack("<I", d[i + 4:i + 8])[0]
        body = d[i + 8:i + 8 + sz]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif cid == b"data":
            raw = body
        i += 8 + sz + (sz & 1)
    if not fmt or raw is None or fmt[0] != 1:
        return None
    ch, rate, sw = fmt[1], fmt[2], fmt[5] // 8
    raw = raw[:int(rate * MAX_SECS) * ch * sw]
    if sw == 1:
        vals = [(b - 128) / 128.0 for b in raw]
    elif sw == 2:
        vals = [v / 32768.0 for v in struct.unpack("<%dh" % (len(raw) // 2), raw[:len(raw) // 2 * 2])]
    else:
        return None
    if ch > 1:
        vals = [sum(vals[i:i + ch]) / ch for i in range(0, len(vals) - ch + 1, ch)]
    return vals, rate


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    sfx = os.path.join(sys.argv[1], "sounds")
    names = SOUNDS
    image, table, smap = bytearray(), [], [0] * 256
    for i, name in enumerate(names):
        r = read_wav(os.path.join(sfx, name))
        if not r or not r[0]:
            continue
        a, src = r
        fade = min(len(a), int(0.02 * src))
        for k in range(fade):
            a[len(a) - fade + k] *= 1 - (k + 1) / fade
        a = resample(lowpass(a, src, RATE), src, RATE)
        peak = max(1e-6, max(abs(v) for v in a))
        a = [v * PEAK / peak for v in a]
        pcm = bytes(sm8(v) for v in a)
        start = (len(image) + PAGE - 1) // PAGE * PAGE
        if start + len(pcm) + TAIL + 1 > RAM or len(table) >= 31:
            continue
        image += bytes([0x80]) * (start - len(image)) + pcm
        loop = len(image)
        image += bytes([0x80]) * TAIL + b"\xff"
        table.append((start // PAGE, VOLUME, int(round(RATE * 2048 / 32552)), loop, len(pcm)))
        if i < 256:
            smap[i] = len(table)
    hdr = bytearray(b"KSFX") + struct.pack(">HH", len(table), len(image) & 0xFFFF)
    for t in table:
        hdr += struct.pack(">BBHHH", *t)
    hdr += bytes(256 - len(hdr))
    open(sys.argv[2], "wb").write(bytes(hdr) + bytes(image) + bytes(smap))
    print("  %s: %d of %d sound effects, %d bytes of 65536 PCM memory" % (sys.argv[2], len(table), len(names), len(image)))


if __name__ == "__main__":
    main()
