#!/usr/bin/env python3
"""zoom.py <in.png> <out.png> <x> <y> <w> <h> [scale]

A 765x503 capture shown whole is about 2 pixels of chat-filter band per
character, and the difference between a plate, a hollow and nothing at all
lives in three rows of pixels. Crop and nearest-neighbour up so the shading is
readable; NEAREST and not a smooth filter, because the question is what the
individual pixels are.
"""
import sys
from PIL import Image

src, dst = sys.argv[1], sys.argv[2]
x, y, w, h = map(int, sys.argv[3:7])
scale = int(sys.argv[7]) if len(sys.argv) > 7 else 3
im = Image.open(src).convert("RGB").crop((x, y, x + w, y + h))
im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
im.save(dst)
print(dst, im.size)
