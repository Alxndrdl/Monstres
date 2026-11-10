#!/usr/bin/env python3
"""Generates the placeholder PWA icons in web/icons/ (standard library only).

Replace them with real artwork later; keep the same file names and sizes.
Usage: python3 tools/make_icons.py
"""
import pathlib
import struct
import zlib

BG = (16, 13, 23)
BODY = (230, 89, 115)
EYE = (245, 245, 250)
PUPIL = (16, 13, 23)


def pixel(u, v, padding):
    """Color at normalized coordinates (0..1) of a simple monster face."""
    # Shrink the drawing toward the center for maskable icons (safe zone).
    u = 0.5 + (u - 0.5) / (1 - 2 * padding)
    v = 0.5 + (v - 0.5) / (1 - 2 * padding)
    for ex in (0.36, 0.64):
        d2 = (u - ex) ** 2 + (v - 0.45) ** 2
        if d2 < 0.045 ** 2:
            return PUPIL
        if d2 < 0.1 ** 2:
            return EYE
    if 0.38 < u < 0.62 and 0.63 < v < 0.68:
        return PUPIL
    if (u - 0.5) ** 2 / 0.32 ** 2 + (v - 0.52) ** 2 / 0.3 ** 2 < 1:
        return BODY
    return BG


def png(size, padding):
    rows = bytearray()
    for y in range(size):
        rows.append(0)  # filter: none
        for x in range(size):
            rows.extend(pixel((x + 0.5) / size, (y + 0.5) / size, padding))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    header = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)  # 8-bit RGB
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b""))


def main():
    out = pathlib.Path(__file__).resolve().parent.parent / "web" / "icons"
    out.mkdir(parents=True, exist_ok=True)
    for name, size, padding in [("icon-192.png", 192, 0.0),
                                ("icon-512.png", 512, 0.0),
                                ("icon-maskable-512.png", 512, 0.1)]:
        (out / name).write_bytes(png(size, padding))
        print("wrote", out / name)


if __name__ == "__main__":
    main()
