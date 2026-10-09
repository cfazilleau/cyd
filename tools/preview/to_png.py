"""Convert the RGB565 dumps written by preview.exe into PNG files (stdlib only)."""
import glob, os, struct, sys, zlib

def to_png(src, dst, scale=2):
    data = open(src, "rb").read()
    w, h = struct.unpack_from("<HH", data)  # header written by preview.cpp
    data = data[4:]
    rows = []
    for y in range(h):
        line = bytearray()
        for x in range(w):
            v = struct.unpack_from("<H", data, (y * w + x) * 2)[0]
            r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
            px = bytes(((r * 527 + 23) >> 6, (g * 259 + 33) >> 6, (b * 527 + 23) >> 6))
            line += px * scale
        rows += [b"\x00" + bytes(line)] * scale
    raw = b"".join(rows)
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(dst, "wb").write(png)

for f in glob.glob(os.path.join(sys.argv[1], "*.rgb565")):
    to_png(f, f[:-7] + ".png")
    print("png:", f[:-7] + ".png")
