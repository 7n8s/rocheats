#!/usr/bin/env python3
"""Builds res/app.ico from the RC monogram artwork.

The source picture is a glossy blue RC on black. It is cropped to the mark,
square-padded, and cut out of the black so the icon sits on any tray colour.
"""
import sys
from PIL import Image, ImageFilter
import numpy as np

SRC = "/home/user/uploads/Glossy Blue RC Monogram Logo.png"
OUT = sys.argv[1] if len(sys.argv) > 1 else "res/app.ico"

im = Image.open(SRC).convert("RGB")
a = np.array(im).astype(np.float32)
lum = a.sum(2)
ys, xs = np.where(lum > 40)
x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1

# square crop around the mark, with a little breathing room
side = max(x1 - x0, y1 - y0)
pad = int(side * 0.06)
side += pad * 2
cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
box = (cx - side // 2, cy - side // 2, cx + side // 2, cy + side // 2)
crop = im.crop(box)

# Cut the black backdrop: the mark is bright where it is opaque, and the glossy
# shadow underneath is a soft alpha ramp rather than a hard edge.
c = np.array(crop).astype(np.float32)
l = c.sum(2)
alpha = np.clip((l - 78.0) / 118.0, 0.0, 1.0)
# A light blur on the alpha keeps the dropout from looking cut with scissors.
alpha_img = Image.fromarray((alpha * 255).astype(np.uint8), "L")
alpha_img = alpha_img.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.GaussianBlur(1.0))
rgba = crop.convert("RGBA")
rgba.putalpha(alpha_img)

# Composite onto a rounded dark tile: the artwork's edges carry a pale fringe
# that would halo on a light taskbar, and a tile reads better at 16 px too.
N = 1024
tile = Image.new("RGBA", (N, N), (0, 0, 0, 0))
mask = Image.new("L", (N, N), 0)
from PIL import ImageDraw
d = ImageDraw.Draw(mask)
r = int(N * 0.23)
d.rounded_rectangle([0, 0, N - 1, N - 1], radius=r, fill=255)
grad = np.zeros((N, N, 3), dtype=np.float32)
yy = np.linspace(0.0, 1.0, N)[:, None]
grad[..., 0] = 0x16 + (0x0A - 0x16) * yy
grad[..., 1] = 0x1A + (0x0C - 0x1A) * yy
grad[..., 2] = 0x1C + (0x0D - 0x1C) * yy
tile = Image.fromarray(grad.astype(np.uint8), "RGB").convert("RGBA")
tile.putalpha(mask)

# a faint cyan glow behind the mark, then the mark itself at 74 %
glow = Image.new("RGBA", (N, N), (0, 0, 0, 0))
ImageDraw.Draw(glow).ellipse([N * 0.10, N * 0.10, N * 0.90, N * 0.90], fill=(0x2E, 0x9B, 0xD6, 60))
from PIL import ImageFilter as F
glow = glow.filter(F.GaussianBlur(N // 12))
tile = Image.alpha_composite(tile, Image.composite(glow, Image.new("RGBA", (N, N), (0, 0, 0, 0)), mask))

inner = int(N * 0.74)
mark = rgba.resize((inner, inner), Image.LANCZOS)
off = (N - inner) // 2
tile.alpha_composite(mark, (off, off))
tile.putalpha(Image.composite(tile.getchannel("A"), Image.new("L", (N, N), 0), mask))

# The splash reuses the tile at a larger size, packed as a PNG resource.
import os
tile.resize((512, 512), Image.LANCZOS).save(os.path.join(os.path.dirname(OUT), "mark.png"))

sizes = [256, 128, 96, 64, 48, 40, 32, 24, 20, 16]
tile.save(OUT, format="ICO", sizes=[(s, s) for s in sizes])
print("wrote", OUT, "from", im.size, "crop", box)
