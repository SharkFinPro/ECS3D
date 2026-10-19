"""PNG writer and the default textures. Standard library only, deterministic output."""
import struct
import zlib

GRID_BACKGROUND = (214, 214, 214)
GRID_THIN = (150, 150, 150)
GRID_HEAVY = (90, 90, 90)

CHECKER_ORANGE = ((255, 150, 50), (200, 90, 15))
CHECKER_BLUE = ((70, 130, 230), (30, 70, 170))

PALETTE = {
    "red": (220, 50, 47),
    "green": (60, 180, 75),
    "blue": (50, 110, 220),
    "yellow": (250, 205, 40),
    "orange": (245, 130, 32),
    "purple": (140, 70, 200),
}


def _chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def write_png(path, width, height, pixel):
    """pixel(x, y) -> (r, g, b). Writes an 8-bit RGB PNG."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            rows.extend(pixel(x, y))
    data = b"\x89PNG\r\n\x1a\n"
    data += _chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    data += _chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    data += _chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


def grid_checker(path, size=512, cells=8, heavy_every=4):
    """Lines sit on cell boundaries and wrap, so the texture tiles seamlessly."""
    cell = size // cells
    heavy = cell * heavy_every

    def on_line(v, period, half_width):
        r = v % period
        return r < half_width or r >= period - half_width

    def pixel(x, y):
        if on_line(x, heavy, 3) or on_line(y, heavy, 3):
            return GRID_HEAVY
        if on_line(x, cell, 1) or on_line(y, cell, 1):
            return GRID_THIN
        return GRID_BACKGROUND

    write_png(path, size, size, pixel)


def checker(path, colors, size=256, cells=8):
    cell = size // cells
    write_png(path, size, size, lambda x, y: colors[((x // cell) + (y // cell)) % 2])


def solid(path, color, size=64):
    write_png(path, size, size, lambda x, y: color)
