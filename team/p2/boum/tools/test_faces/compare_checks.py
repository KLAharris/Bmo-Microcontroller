#!/usr/bin/env python3
"""Compare out/face_Rosto-XX.ppm (from test_faces) with the slice_faces.py
check images in ../figma_faces/check/. Both are the firmware blend expanded
to 8-bit, so they must match exactly.

All 13 faces, Rosto-01 included (FACE_IDLE is the Figma face since §18).
"""
import glob
import os
import sys

import numpy as np
from PIL import Image

here = os.path.dirname(os.path.abspath(__file__))
check_dir = os.path.join(here, "..", "figma_faces", "check")
SKIP = {}

fails = 0
outs = sorted(glob.glob(os.path.join(here, "out", "face_Rosto-*.ppm")))
if not outs:
    sys.exit("no out/face_Rosto-*.ppm: run ./test_faces first")
for path in outs:
    face = os.path.basename(path)[5:-4]
    if face in SKIP:
        print(f"{face}: skipped ({SKIP[face]})")
        continue
    got = np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)
    want = np.asarray(Image.open(os.path.join(check_dir, f"check_{face}.png")).convert("RGB"), dtype=np.int16)
    n = int((np.abs(got - want).max(-1) > 0).sum())
    print(f"{face}: {'match' if n == 0 else f'{n} px differ'}")
    fails += n != 0

print("All check images match" if not fails else f"{fails} face(s) differ")
sys.exit(1 if fails else 0)
