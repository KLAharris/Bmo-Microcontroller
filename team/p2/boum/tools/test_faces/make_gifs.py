#!/usr/bin/env python3
"""Turn out/anim/Rosto-XX_NNN.ppm (written by ./test_faces --anim, 6 s per
face, one frame every 50 ms) into:
  out/gif/Rosto-XX.gif      real-time animation of the first 6 s
  out/gif/sheet_Rosto-XX.png  contact sheet: every 4th frame (0.2 s apart)
then delete the frames (about 690 MB). Run it with `make gifs`.
"""
import glob
import os
import re
import sys

from PIL import Image

here = os.path.dirname(os.path.abspath(__file__))
src = os.path.join(here, "out", "anim")
dst = os.path.join(here, "out", "gif")
os.makedirs(dst, exist_ok=True)

frames = {}
for path in sorted(glob.glob(os.path.join(src, "Rosto-*_*.ppm"))):
    m = re.match(r"(Rosto-\d\d)_(\d+)\.ppm", os.path.basename(path))
    if m:
        frames.setdefault(m.group(1), []).append(path)
if not frames:
    sys.exit("no out/anim/*.ppm: run `make gifs` (it writes them first)")

for face, paths in sorted(frames.items()):
    imgs = [Image.open(p).convert("RGB") for p in paths]
    imgs[0].save(os.path.join(dst, f"{face}.gif"), save_all=True, append_images=imgs[1:],
                 duration=50, loop=0)
    pick = imgs[::4]
    cols = 6
    rows = (len(pick) + cols - 1) // cols
    w, h = 240, 160
    sheet = Image.new("RGB", (cols * w, rows * h), (40, 40, 40))
    for i, im in enumerate(pick):
        sheet.paste(im.resize((w, h), Image.NEAREST), ((i % cols) * w, (i // cols) * h))
    sheet.save(os.path.join(dst, f"sheet_{face}.png"))
    print(f"{face}: {len(imgs)} frames -> gif + sheet")
    for p in paths:
        os.remove(p)

try:
    os.rmdir(src)                                  # only if nothing else is left
except OSError:
    pass
