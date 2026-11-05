"""Writes the picture calibration patterns to assets/patterns/ (see app/picture.cpp).

    python tools/make_patterns.py

Standard library only. Every pattern is 1280x720, the handheld screen's own
size, in full-range RGB -- mpv shows a PNG as is, so a code value here is the
code value on the screen (before the user's brightness/contrast/gamma).
"""

import os
import struct
import sys
import zlib

W, H = 1280, 720

# A 5x7 bitmap font: just what the labels need.
FONT = {
    "0": ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    "2": ["01110", "10001", "00001", "00010", "00100", "01000", "11111"],
    "3": ["11110", "00001", "00001", "01110", "00001", "00001", "11110"],
    "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    "5": ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
    "6": ["00110", "01000", "10000", "11110", "10001", "10001", "01110"],
    "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    "9": ["01110", "10001", "10001", "01111", "00001", "00010", "01100"],
    ".": ["00000", "00000", "00000", "00000", "00000", "01100", "01100"],
}


class Image:
    def __init__(self, bg):
        self.px = [bytearray(bytes(bg) * W) for _ in range(H)]

    def rect(self, x0, y0, x1, y1, c):
        row = bytes(c) * (x1 - x0)
        for y in range(y0, y1):
            self.px[y][x0 * 3:x1 * 3] = row

    def set(self, x, y, c):
        self.px[y][x * 3:x * 3 + 3] = bytes(c)

    def text(self, s, cx, y, scale, c):
        """Draws `s` centred on cx, top at y."""
        width = len(s) * 6 * scale - scale
        x = cx - width // 2
        for ch in s:
            for gy, line in enumerate(FONT[ch]):
                for gx, bit in enumerate(line):
                    if bit == "1":
                        self.rect(x + gx * scale, y + gy * scale,
                                  x + (gx + 1) * scale, y + (gy + 1) * scale, c)
            x += 6 * scale

    def save(self, path):
        raw = b"".join(b"\x00" + bytes(r) for r in self.px)

        def chunk(tag, data):
            body = tag + data
            return (struct.pack(">I", len(data)) + body +
                    struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF))

        png = (b"\x89PNG\r\n\x1a\n" +
               chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) +
               chunk(b"IDAT", zlib.compress(raw, 9)) +
               chunk(b"IEND", b""))
        with open(path, "wb") as f:
            f.write(png)


def grey(v):
    return (v, v, v)


def pct(p):
    return round(p * 255 / 100)


def black_level():
    # Bars at 1%..8% on true black: the darkest detail the screen still shows.
    im = Image(grey(0))
    n, bw, gap = 8, 110, 20
    x0 = (W - (n * bw + (n - 1) * gap)) // 2
    for i in range(n):
        x = x0 + i * (bw + gap)
        im.rect(x, 140, x + bw, 520, grey(pct(i + 1)))
        im.text(str(i + 1), x + bw // 2, 550, 6, grey(140))
    return im


def white_level():
    # Bars at 90%..99% on full white: the brightest detail before it clips.
    im = Image(grey(255))
    n, bw, gap = 10, 90, 14
    x0 = (W - (n * bw + (n - 1) * gap)) // 2
    for i in range(n):
        x = x0 + i * (bw + gap)
        im.rect(x, 140, x + bw, 520, grey(pct(90 + i)))
        im.text(str(90 + i), x + bw // 2, 550, 5, grey(110))
    return im


def greyscale():
    # Eleven steps 0..100%, then a smooth ramp: steps that merge, a tint, or
    # bands in the ramp all show here.
    im = Image(grey(0))
    n = 11
    for i in range(n):
        x0, x1 = i * W // n, (i + 1) * W // n
        v = pct(i * 10)
        im.rect(x0, 0, x1, 380, grey(v))
        im.text(str(i * 10), (x0 + x1) // 2, 320, 4, grey(0 if v > 128 else 200))
    for x in range(W):
        v = round(x * 255 / (W - 1))
        im.rect(x, 400, x + 1, H, grey(v))
    return im


def gamma_chart():
    # Each block is 1-px black/white lines -- 50% of the screen's light whatever
    # its gamma -- around a solid patch whose code is 50% light at gamma g. The
    # patch that blends into its lines is the gamma the screen has.
    im = Image(grey(24))
    gammas = ["1.8", "2.0", "2.2", "2.4", "2.6"]
    bw, gap = 200, 40
    x0 = (W - (len(gammas) * bw + (len(gammas) - 1) * gap)) // 2
    y0, y1 = 200, 400
    for i, g in enumerate(gammas):
        x = x0 + i * (bw + gap)
        for y in range(y0, y1):
            im.rect(x, y, x + bw, y + 1, grey(255 if y % 2 == 0 else 0))
        v = round(255 * 0.5 ** (1 / float(g)))
        im.rect(x + 50, y0 + 50, x + bw - 50, y1 - 50, grey(v))
        im.text(g, x + bw // 2, 440, 6, grey(220))
    return im


def colour():
    # 75% bars, 100% bars, then skin tones from light to dark.
    im = Image(grey(0))
    bars = [(1, 1, 1), (1, 1, 0), (0, 1, 1), (0, 1, 0), (1, 0, 1), (1, 0, 0), (0, 0, 1)]
    for i, (r, g, b) in enumerate(bars):
        x0, x1 = i * W // 7, (i + 1) * W // 7
        im.rect(x0, 0, x1, 300, (191 * r, 191 * g, 191 * b))
        im.rect(x0, 300, x1, 420, (255 * r, 255 * g, 255 * b))
    skins = [(247, 213, 188), (234, 189, 157), (212, 158, 122),
             (180, 124, 88), (141, 85, 54), (96, 58, 36)]
    n, sw, gap = len(skins), 170, 20
    x0 = (W - (n * sw + (n - 1) * gap)) // 2
    for i, c in enumerate(skins):
        x = x0 + i * (sw + gap)
        im.rect(x, 460, x + sw, 660, c)
    return im


def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "patterns")
    os.makedirs(out, exist_ok=True)
    for name, fn in [("black", black_level), ("white", white_level),
                     ("greyscale", greyscale), ("gamma", gamma_chart), ("colour", colour)]:
        path = os.path.join(out, name + ".png")
        fn().save(path)
        print(path, os.path.getsize(path))


if __name__ == "__main__":
    sys.exit(main())
