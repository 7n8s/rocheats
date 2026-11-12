#!/usr/bin/env python3
"""Builds res/logos.bin: the official product artwork, ready to draw.

Each product becomes one opaque SIZE x SIZE tile, so the app can simply blit
it and round the corners. Artwork that already has a dark backdrop is scaled to
fill the tile; a bare logo is centred on the app's dark tile colour instead.

    python3 tools/make_images.py <source folder> [res/logos.bin]

The source folder must contain the files named in SOURCES below. The blob is
read by main.cpp from resource 101 (RCDATA) and laid out as:

    "RCL1"      4 bytes
    u32 tiles   number of tiles (10)
    u32 size    pixels per side (96)
    ...         tiles x size x size x 4 bytes of RGBA, in ORDER
"""
import sys
from pathlib import Path

from PIL import Image

SIZE = 96
ORDER = ["volt", "wave", "potassium", "isaeva", "matcha",
         "serotonin", "seliware", "lucid", "axis", "codex"]
SOURCES = {
    "volt": "volt.png",                 # cdn.weao.gg/slug/volt/logo.png
    "wave": "wave.png",                 # cdn.weao.gg/slug/wave/logo.png
    "potassium": "potassium.webp",      # cdn.weao.gg/slug/potassium/logo.png
    "isaeva": "isaeva.png",             # cdn.weao.gg/slug/isaeva/logo.png
    "matcha": "matcha.webp",            # scriptblox.com/images/exec/thumbs/matcha.webp
    "serotonin": "serotonin.webp",      # scriptblox.com/images/exec/thumbs/serotonin.webp
    "seliware": "seliware.png",         # cdn.weao.gg/slug/seliware/logo.png
    "lucid": "lucid_fav.png",           # getlucid.gg/assets/images/brand/favicon.png
    "axis": "axis_pub.ico",             # useaxis.cc/public/axisico.ico
    "codex": "codex2.png",              # www.codex.lol/static/icons/android-chrome-512x512.png
}
TILE = (0x14, 0x16, 0x1D)   # backdrop for logos that come without one
PAD = 0.76                  # how much of the tile a bare logo may fill


def has_backdrop(im):
    """True when the artwork already fills its own square (no transparency)."""
    if im.mode not in ("RGBA", "LA", "P"):
        return True
    alpha = im.convert("RGBA").getchannel("A")
    lo, hi = alpha.getextrema()
    if lo > 250:
        return True  # fully opaque
    # Transparent corners mean a bare mark on a transparent background.
    w, h = alpha.size
    corners = [alpha.getpixel(p) for p in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1))]
    return max(corners) > 200


def cover(im, size):
    """Scale to fill the square, cropping the overflow (centred)."""
    w, h = im.size
    scale = max(size / w, size / h)
    im = im.resize((max(1, round(w * scale)), max(1, round(h * scale))), Image.LANCZOS)
    w, h = im.size
    left, top = (w - size) // 2, (h - size) // 2
    return im.crop((left, top, left + size, top + size))


def contain(im, size, pad):
    """Fit the whole mark inside the square, centred, on the tile colour."""
    tile = Image.new("RGBA", (size, size), TILE + (255,))
    box = int(size * pad)
    w, h = im.size
    scale = min(box / w, box / h)
    im = im.resize((max(1, round(w * scale)), max(1, round(h * scale))), Image.LANCZOS)
    tile.alpha_composite(im, ((size - im.width) // 2, (size - im.height) // 2))
    return tile


def build(src, dst):
    chunks = []
    for name in ORDER:
        path = Path(src) / SOURCES[name]
        im = Image.open(path).convert("RGBA")
        tile = cover(im, SIZE) if has_backdrop(im) else contain(im, SIZE, PAD)
        tile = tile.convert("RGBA")
        chunks.append(tile.tobytes())
        print(f"  {name:10s} <- {SOURCES[name]:16s} {im.size[0]}x{im.size[1]} "
              f"{'backdrop' if has_backdrop(im) else 'bare mark'}")
    import struct
    head = b"RCL1" + struct.pack("<II", len(chunks), SIZE)
    blob = head + b"".join(chunks)
    Path(dst).write_bytes(blob)
    print(f"wrote {dst}: {len(blob)} bytes ({len(chunks)} tiles of {SIZE}x{SIZE}, "
          f"header {len(head)})")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    build(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "res/logos.bin")
