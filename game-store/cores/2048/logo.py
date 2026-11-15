"""Original 48x48 RGB565 Sonix 2048 artwork; standard library only."""
import struct
import sys
from pathlib import Path

SIDE = 48
pixels = [0] * (SIDE * SIDE)

def color(rgb):
    r, g, b = (int(rgb[i:i+2], 16) for i in (0, 2, 4))
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)

def rect(x, y, w, h, rgb):
    c = color(rgb)
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            pixels[yy * SIDE + xx] = c

DIGITS = {"2": ["111", "001", "111", "100", "111"],
          "4": ["101", "101", "111", "001", "001"],
          "8": ["111", "101", "111", "101", "111"],
          "1": ["010", "110", "010", "010", "111"],
          "6": ["111", "100", "111", "101", "111"]}

def tile(x, y, number, rgb):
    rect(x + 1, y, 18, 20, rgb)
    rect(x, y + 1, 20, 18, rgb)
    scale = 2
    width = (len(number) * 4 - 1) * scale
    start = x + (20 - width) // 2
    for i, d in enumerate(number):
        for yy, row in enumerate(DIGITS[d]):
            for xx, bit in enumerate(row):
                if bit == "1":
                    rect(start + i * 4 * scale + xx * scale,
                         y + 5 + yy * scale, scale, scale, "392f29")

rect(0, 0, SIDE, SIDE, "786858")
tile(3, 3, "2", "eee4d4")
tile(25, 3, "4", "eddebd")
tile(3, 25, "8", "f7ae70")
tile(25, 25, "16", "f68f56")
Path(sys.argv[1]).write_bytes(struct.pack("<" + "H" * len(pixels), *pixels))
