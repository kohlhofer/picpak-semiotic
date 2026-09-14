# SPDX-FileCopyrightText: 2026 Field Bureau & Werkstatt LLC
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Convert a binary PPM (P6) to PNG with the standard library only.

usage: ppm2png.py IN.ppm OUT.png [SCALE]
"""
import struct
import sys
import zlib

src, dst = sys.argv[1], sys.argv[2]
scale = int(sys.argv[3]) if len(sys.argv) > 3 else 1

with open(src, "rb") as f:
    data = f.read()
parts = data.split(maxsplit=4)
assert parts[0] == b"P6", "not a binary PPM"
w, h, pixels = int(parts[1]), int(parts[2]), parts[4]

rows = []
for y in range(h):
    row = pixels[y * w * 3:(y + 1) * w * 3]
    wide = b"".join(row[x * 3:x * 3 + 3] * scale for x in range(w))
    rows.extend([b"\x00" + wide] * scale)


def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)


png = b"\x89PNG\r\n\x1a\n"
png += chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(b"".join(rows), 9))
png += chunk(b"IEND", b"")
with open(dst, "wb") as f:
    f.write(png)
