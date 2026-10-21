"""
slice_faces.py — Figma face exports -> coloured A4 stencils for the STM32.

  cd ~/STM32/boum/tools
  python3 slice_faces.py                     # figma_faces/ -> ../Core/Inc/bmo_faces.h
  python3 slice_faces.py --debug DIR         # also save role-coloured debug images

Input:  figma_faces/Rosto-XX.png, 480x270, transparent background (CLAUDE.md §15).
Output: ../Core/Inc/bmo_faces.h and figma_faces/check/check_Rosto-XX.png.

Pipeline per face:
  1. Paste the export at (0, 25) on a 480x320 canvas.
  2. Palette ("inks"): colours of opaque AND semi-transparent pixels (PNG alpha
     is straight, so RGB is already the un-premultiplied colour), weighted by
     alpha. Close colours merge; saturated colours of the same hue merge too
     (21's red gradient -> one flat red). Edge colours that are a blend of two
     inks are not inks.
  3. Each pixel becomes one ink, or two inks blended at an edge. Inks are drawn
     back to front (outer outline first, enclosed fills/highlights last); the
     two alphas are solved so that drawing them in that order gives back the
     original pixel.
  4. Roles: blush inks (soft or pink) are always PART_EXTRA, by colour.
     The rest is split into connected pieces: centre band = mouth; on each
     side the main piece is the eye, a piece above it is the brow, anything
     else (21's "^^" marks) is extra.
  5. One stencil per role x ink, cropped tight, x and width rounded to even.
  6. Rebuild every face from the packed nibbles with the firmware's RGB565
     mix() and save it as check/check_Rosto-XX.png.

A4 = 4-bit alpha, 2 px per byte, HIGH nibble = LEFT pixel (as face_gfx.h expects).
"""
import argparse
import colorsys
import glob
import os
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

CANVAS_W, CANVAS_H = 480, 320
TOP = 25                                   # (320 - 270) / 2
BG_RGB = (0xC9, 0xE4, 0xC3)                # "Face - 1st Color"
BG_565 = 0xCF38

MIN_ALPHA = 0.10        # palette ignores fainter pixels (colour too unreliable)
MIN_WEIGHT = 30.0       # an ink needs this much alpha-weighted area
MERGE_DIST = 48.0       # RGB distance: same ink
MIX_TOL = 20.0          # RGB distance from the line between two inks: edge blend
HUE_MERGE_DEG = 12.0    # saturated colours this close in hue: same ink (gradients)
PAIR_PENALTY = 6.0      # prefer one ink over a two-ink blend when both fit
PURE_TOL = 12.0         # pixel this close to an ink shows that ink is present
NEAR_PX = 3             # ...and the ink may be used this many pixels around it
SPECK_WEIGHT = 8        # pieces with less total A4 than this join their neighbour
SOFT_ALPHA = 0.6        # ink whose median alpha is below this is a soft blush
BLUSH_INKS = [(255, 200, 216)]             # hard-edged blush colours (13's pink)
CENTRE_X = (160, 320)   # piece centre inside this band -> mouth
BROW_X_MARGIN = 20      # brow must overlap the eye's x range (+ this margin)

ROLES = ["PART_EYE_L", "PART_EYE_R", "PART_BROW_L", "PART_BROW_R", "PART_MOUTH", "PART_EXTRA"]
ROLE_TAG = ["eye_l", "eye_r", "brow_l", "brow_r", "mouth", "extra"]
EYE_L, EYE_R, BROW_L, BROW_R, MOUTH, EXTRA = range(6)
PART_STRUCT_BYTES = 16  # sizeof(face_part_t) on Cortex-M7


# ------------------------------------------------------------------ helpers
def to565(c):
    r, g, b = (int(round(v)) for v in c)
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def expand565(p):
    """RGB565 array -> 8-bit RGB array, as the panel shows it."""
    r = (p >> 11) & 0x1F
    g = (p >> 5) & 0x3F
    b = p & 0x1F
    return np.stack([(r * 255 + 15) // 31, (g * 255 + 31) // 63, (b * 255 + 15) // 31], -1)


def fw_mix(fg, bg, a):
    """Exact copy of mix() in gfx_spi.c, vectorised. fg scalar, bg/a arrays."""
    out = np.zeros_like(bg)
    for shift, mask in ((11, 0x1F), (5, 0x3F), (0, 0x1F)):
        f = (fg >> shift) & mask
        b = (bg >> shift) & mask
        out |= ((f * a + b * (15 - a) + 7) // 15) << shift
    return out


def hue_sat_val(c):
    return colorsys.rgb_to_hsv(*(v / 255.0 for v in c))


def same_hue(c, d):
    h1, s1, v1 = hue_sat_val(c)
    h2, s2, v2 = hue_sat_val(d)
    if min(s1, s2) < 0.5 or min(v1, v2) < 0.3:
        return False
    dh = abs(h1 - h2) * 360.0
    return min(dh, 360.0 - dh) < HUE_MERGE_DEG


def seg_dist(c, p, q):
    """Distance from colour c to segment p-q, and the weight t of p."""
    d = p - q
    n = float(d @ d)
    if n == 0:
        return float(np.linalg.norm(c - q)), 1.0
    t = float(np.clip((c - q) @ d / n, 0.0, 1.0))
    return float(np.linalg.norm(c - (t * p + (1 - t) * q))), t


# ------------------------------------------------------------------ palette
def build_palette(rgb, a):
    """Returns (inks Kx3 float, colour->ink map for gradient shades).

    Only same-hue gradient shades are force-mapped to their ink (21's hearts
    become flat red). Every other colour is left to decompose(), which picks
    the nearest ink or a two-ink edge blend.
    """
    m = a >= MIN_ALPHA
    cols, inv = np.unique(rgb[m], axis=0, return_inverse=True)
    wsum = np.bincount(inv.ravel(), weights=a[m])
    inks, members = [], {}
    for k in np.argsort(-wsum):
        c = cols[k].astype(float)
        if inks:
            hue_j = next((j for j, e in enumerate(inks) if same_hue(c, e)), None)
            if hue_j is not None:
                members[tuple(cols[k])] = hue_j
                continue                      # gradient shade: flat ink
            d = [np.linalg.norm(c - e) for e in inks]
            mix = min((seg_dist(c, inks[i], inks[j])[0]
                       for i in range(len(inks)) for j in range(i + 1, len(inks))),
                      default=np.inf)
            if mix < MIX_TOL and mix < min(d):
                continue                      # blend of two inks at an edge
            if min(d) < MERGE_DIST:
                continue                      # a shade of an existing ink
        if wsum[k] >= MIN_WEIGHT:
            members[tuple(cols[k])] = len(inks)
            inks.append(c)
    return np.array(inks), members


def nearest_ink(rgb, inks):
    d = np.linalg.norm(rgb[..., None, :] - inks[None, None], axis=-1)
    return np.argmin(d, axis=-1)


def layer_order(rgb, a, inks):
    """Back-to-front order: inks touching the transparent outside go first."""
    near = nearest_ink(rgb, inks)
    opaque = a >= 0.5
    outer = []
    for k in range(len(inks)):
        mk = opaque & (near == k)
        edge = mk & ~ndimage.binary_erosion(mk)
        if not edge.any():
            outer.append((0.0, 0))
            continue
        touches_out = edge & ndimage.binary_dilation(~opaque)
        outer.append((touches_out.sum() / edge.sum(), int(mk.sum())))
    return sorted(range(len(inks)), key=lambda k: outer[k], reverse=True)


# ------------------------------------------------------------ decomposition
def decompose(rgb, a, inks, members, layer_of):
    """Per-ink alpha planes (float 0..1) that composite back to the original."""
    K = len(inks)
    planes = np.zeros((K,) + a.shape)
    ys, xs = np.nonzero(a > 0)
    c = rgb[ys, xs].astype(float)
    al = a[ys, xs]

    # colours that were merged into an ink while building the palette: one ink
    mapped = np.array([members.get(tuple(v), -1) for v in rgb[ys, xs]])

    # an ink may only be used where it really appears within NEAR_PX pixels,
    # so an edge blend is never explained by an ink from elsewhere on the face
    all_err = np.linalg.norm(rgb[..., None, :].astype(float) - inks[None, None], axis=-1)
    near = np.stack([ndimage.binary_dilation((all_err[..., k] < PURE_TOL) & (a >= MIN_ALPHA),
                                             iterations=NEAR_PX) for k in range(K)])
    ok = near[:, ys, xs].T                                             # N x K

    # otherwise best of: one ink, or a blend of two inks
    single_err = np.linalg.norm(c[:, None] - inks[None], axis=-1)     # N x K
    single = np.argmin(np.where(ok, single_err, single_err + 1e6), axis=1)
    err = single_err[np.arange(len(c)), single] + np.where(ok.any(1), 0, 1e6)
    pick_i = single.copy()
    pick_j = np.full(len(c), -1)
    pick_t = np.ones(len(c))
    for i in range(K):
        for j in range(i + 1, K):
            p, q = inks[i], inks[j]
            d = p - q
            t = np.clip((c - q) @ d / float(d @ d), 0, 1)
            e = np.linalg.norm(c - (t[:, None] * p + (1 - t[:, None]) * q), axis=1) + PAIR_PENALTY
            better = (e < err) & (t > 0.02) & (t < 0.98) & ok[:, i] & ok[:, j]
            err[better] = e[better]
            pick_i[better], pick_j[better], pick_t[better] = i, j, t[better]

    use_map = mapped >= 0
    pick_i[use_map], pick_j[use_map], pick_t[use_map] = mapped[use_map], -1, 1.0

    one = pick_j < 0
    planes[pick_i[one], ys[one], xs[one]] = al[one]

    two = ~one
    i, j, t, aa = pick_i[two], pick_j[two], pick_t[two], al[two]
    i_top = np.array([layer_of[v] for v in i]) > np.array([layer_of[v] for v in j])
    top = np.where(i_top, i, j)
    bot = np.where(i_top, j, i)
    t_top = np.where(i_top, t, 1 - t)
    a_top = aa * t_top
    a_bot = np.where(a_top < 0.999, aa * (1 - t_top) / np.maximum(1 - a_top, 1e-6), 0.0)
    planes[top, ys[two], xs[two]] = a_top
    planes[bot, ys[two], xs[two]] = np.clip(a_bot, 0, 1)
    return planes


# ------------------------------------------------------------------- roles
def label_pieces(mask, weight):
    """8-connected pieces; specks join the nearest real piece."""
    lab, n = ndimage.label(mask, structure=np.ones((3, 3)))
    if n == 0:
        return lab, []
    w = ndimage.sum(weight, lab, index=np.arange(1, n + 1))
    keep = [k + 1 for k in range(n) if w[k] >= SPECK_WEIGHT]
    if not keep:
        keep = [int(np.argmax(w)) + 1]
    big = np.isin(lab, keep)
    _, (iy, ix) = ndimage.distance_transform_edt(~big, return_indices=True)
    lab = np.where(mask, lab[iy, ix], 0)
    return lab, keep


def bbox(m):
    ys, xs = np.nonzero(m)
    return xs.min(), ys.min(), xs.max() + 1, ys.max() + 1


def assign_roles(q, inks, extra_inks):
    """Returns list of (role, ink, mask) groups."""
    groups = []

    # blush inks: always extra, one part per connected blob
    for k in extra_inks:
        lab, keep = label_pieces(q[k] > 0, q[k])
        for l in keep:
            groups.append((EXTRA, k, lab == l))

    body = [k for k in range(len(inks)) if k not in extra_inks]
    if not body:
        return groups
    wsum = sum(q[k].astype(int) for k in body)
    lab, keep = label_pieces(wsum > 0, wsum)

    info = {}
    for l in keep:
        m = lab == l
        x0, y0, x1, y1 = bbox(m)
        ys, xs = np.nonzero(m)
        info[l] = dict(mask=m, box=(x0, y0, x1, y1), cx=xs.mean(), cy=ys.mean(), w=wsum[m].sum())

    role_of = {}
    for side, eye, brow in (("L", EYE_L, BROW_L), ("R", EYE_R, BROW_R)):
        pcs = [l for l in keep if (info[l]["cx"] < CENTRE_X[0]) == (side == "L")
               and not CENTRE_X[0] <= info[l]["cx"] <= CENTRE_X[1]]
        if not pcs:
            continue
        top_w = max(info[l]["w"] for l in pcs)
        main = max((l for l in pcs if info[l]["w"] >= 0.4 * top_w), key=lambda l: info[l]["cy"])
        role_of[main] = eye
        ex0, _, ex1, _ = info[main]["box"]
        for l in pcs:
            if l == main:
                continue
            x0, _, x1, y1 = info[l]["box"]
            above = y1 <= info[main]["cy"]
            overlap = x1 > ex0 - BROW_X_MARGIN and x0 < ex1 + BROW_X_MARGIN
            role_of[l] = brow if (above and overlap) else EXTRA
    for l in keep:
        role_of.setdefault(l, MOUTH)

    # merge same-role pieces into one part, except extras (kept separate)
    for role in (EYE_L, EYE_R, BROW_L, BROW_R, MOUTH):
        m = np.isin(lab, [l for l in keep if role_of[l] == role])
        if m.any():
            for k in body:
                groups.append((role, k, m))
    for l in keep:
        if role_of[l] == EXTRA:
            for k in body:
                groups.append((EXTRA, k, lab == l))
    return groups


# ------------------------------------------------------------------ packing
def pack_a4(a):
    h, w = a.shape
    flat = a.astype(np.uint8).ravel()
    return list((flat[0::2] << 4) | flat[1::2]), w, h


def unpack_a4(data, w, h):
    b = np.array(data, dtype=np.uint8)
    out = np.empty(w * h, dtype=np.int64)
    out[0::2] = b >> 4
    out[1::2] = b & 0x0F
    return out.reshape(h, w)


def slice_face(path):
    src = np.array(Image.open(path).convert("RGBA"))
    if src.shape[:2] != (270, 480):
        sys.exit(f"{path}: expected 480x270, got {src.shape[1]}x{src.shape[0]}")
    canvas = np.zeros((CANVAS_H, CANVAS_W, 4), dtype=np.uint8)
    canvas[TOP:TOP + 270] = src
    rgb = canvas[..., :3]
    a = canvas[..., 3] / 255.0

    inks, members = build_palette(rgb, a)
    order = layer_order(rgb, a, inks)
    layer_of = {k: n for n, k in enumerate(order)}
    planes = decompose(rgb, a, inks, members, layer_of)
    q = np.rint(planes * 15).astype(np.uint8)

    extra_inks = []
    for k in range(len(inks)):
        vals = planes[k][q[k] > 0]
        soft = vals.size and np.median(vals) < SOFT_ALPHA
        pink = any(np.linalg.norm(inks[k] - np.array(b)) < MERGE_DIST for b in BLUSH_INKS)
        if soft or pink:
            extra_inks.append(k)

    parts = []
    for role, k, m in assign_roles(q, inks, extra_inks):
        stencil = np.where(m, q[k], 0)
        if not stencil.any():
            continue
        x0, y0, x1, y1 = bbox(stencil > 0)
        x0 -= x0 % 2
        x1 += x1 % 2
        data, w, h = pack_a4(stencil[y0:y1, x0:x1])
        parts.append(dict(role=role, ink=k, layer=layer_of[k], x=int(x0), y=int(y0),
                          w=w, h=h, color=to565(inks[k]), rgb=inks[k], data=data))
    parts.sort(key=lambda p: (p["layer"], p["role"], p["y"], p["x"]))

    # reference: original over the mint, then the best RGB565 can show
    ref = BG_RGB * (1 - a[..., None]) + rgb * a[..., None]
    r, g, b = (np.rint(ref[..., i]).astype(np.int64) for i in range(3))
    ref_q = expand565(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    return dict(inks=inks, order=order, extra_inks=extra_inks, parts=parts, ref=ref, ref_q=ref_q)


def rebuild(parts):
    """Decode the packed bytes and blend exactly like gfx_blend_sprite()."""
    screen = np.full((CANVAS_H, CANVAS_W), BG_565, dtype=np.int64)
    for p in parts:
        a = unpack_a4(p["data"], p["w"], p["h"])
        win = screen[p["y"]:p["y"] + p["h"], p["x"]:p["x"] + p["w"]]
        mixed = fw_mix(p["color"], win, a)
        win[:] = np.where(a == 15, p["color"], np.where(a == 0, win, mixed))
    return expand565(screen)


# ------------------------------------------------------------------ output
def c_name(face):
    return face.lower().replace("-", "_")


def write_header(dst, faces):
    L = [
        "/* bmo_faces.h — GENERATED by tools/slice_faces.py. Do not edit. */",
        "/* Figma faces (BMO Face Templates, Community) as coloured A4 stencils.",
        " * A4 alpha, 2 px/byte, high nibble = left pixel. Lives in flash (const).",
        " * Screen 480x320, mint background 0xCF38. Draw each face's parts in table",
        " * order (layer 0 = back) with gfx_blend_sprite(a4, w, h, x, y, color). */",
        "#ifndef BMO_FACES_H",
        "#define BMO_FACES_H",
        "",
        "#include <stdint.h>",
        "",
        "typedef enum {",
    ]
    L += [f"    {r}," for r in ROLES]
    L += [
        "} face_part_role_t;",
        "",
        "typedef struct {",
        "    const uint8_t *a4;     /* w * h / 2 bytes                       */",
        "    int16_t  x, y;         /* top-left on the 480x320 screen         */",
        "    uint16_t w, h;         /* w is even                              */",
        "    uint16_t color;        /* RGB565                                 */",
        "    uint8_t  layer;        /* 0 = back; table is sorted by layer     */",
        "    uint8_t  role;         /* face_part_role_t                       */",
        "} face_part_t;",
        "",
        "typedef struct {",
        "    const char        *name;",
        "    const face_part_t *parts;",
        "    uint8_t            count;",
        "} face_def_t;",
        "",
        "typedef enum {",
    ]
    L += [f"    BMO_{c_name(f).upper()}," for f, _ in faces]
    L += ["    BMO_FACE_COUNT", "} bmo_face_id_t;", ""]

    for face, res in faces:
        n = c_name(face)
        for i, p in enumerate(res["parts"]):
            arr = f"{n}_{i:02d}_{ROLE_TAG[p['role']]}_a4"
            p["arr"] = arr
            L.append(f"/* {face} part {i}: {ROLE_TAG[p['role']]}, ink #{to_hex(p['rgb'])}, "
                     f"{p['w']} x {p['h']}, {len(p['data'])} bytes */")
            L.append(f"static const uint8_t {arr}[{len(p['data'])}] = {{")
            for r in range(0, len(p["data"]), 16):
                L.append("    " + ", ".join(f"0x{v:02X}" for v in p["data"][r:r + 16]) + ",")
            L.append("};")
        L.append("")
        L.append(f"static const face_part_t {n}_parts[{len(res['parts'])}] = {{")
        for p in res["parts"]:
            L.append(f"    {{ {p['arr']}, {p['x']:3d}, {p['y']:3d}, {p['w']:3d}, {p['h']:3d}, "
                     f"0x{p['color']:04X}, {p['layer']}, {ROLES[p['role']]} }},")
        L.append("};")
        L.append("")

    L.append("static const face_def_t bmo_faces[BMO_FACE_COUNT] = {")
    for face, res in faces:
        n = c_name(face)
        L.append(f'    {{ "{face}", {n}_parts, {len(res["parts"])} }},')
    L += ["};", "", "#endif", ""]
    with open(dst, "w") as f:
        f.write("\n".join(L))


def to_hex(c):
    return "".join(f"{int(round(v)):02X}" for v in c)


def debug_image(res, out):
    """Each part tinted by role: eyes blue, brows orange, mouth red, extra purple."""
    tint = {EYE_L: (40, 90, 220), EYE_R: (40, 160, 255), BROW_L: (230, 130, 0),
            BROW_R: (255, 190, 60), MOUTH: (220, 30, 30), EXTRA: (150, 40, 200)}
    img = np.full((CANVAS_H, CANVAS_W, 3), 255, dtype=np.uint8)
    for p in res["parts"]:
        a = unpack_a4(p["data"], p["w"], p["h"])[..., None] / 15.0
        win = img[p["y"]:p["y"] + p["h"], p["x"]:p["x"] + p["w"]]
        win[:] = np.rint(win * (1 - a) + np.array(tint[p["role"]]) * a).astype(np.uint8)
    Image.fromarray(img).save(out)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("src", nargs="?", default=os.path.join(here, "figma_faces"))
    ap.add_argument("dst", nargs="?", default=os.path.join(here, "..", "Core", "Inc", "bmo_faces.h"))
    ap.add_argument("--debug", metavar="DIR", help="save role-coloured images here")
    args = ap.parse_args()

    files = sorted(glob.glob(os.path.join(args.src, "Rosto-*.png")))
    if not files:
        sys.exit(f"no Rosto-*.png in {args.src}")
    check_dir = os.path.join(args.src, "check")
    os.makedirs(check_dir, exist_ok=True)
    if args.debug:
        os.makedirs(args.debug, exist_ok=True)

    faces = []
    for f in files:
        face = os.path.basename(f)[:-4]
        res = slice_face(f)
        faces.append((face, res))
        out = rebuild(res["parts"])
        Image.fromarray(out.astype(np.uint8)).save(os.path.join(check_dir, f"check_{face}.png"))
        diff = np.abs(out - res["ref_q"]).max(-1)
        res["err_max"] = int(diff.max())
        res["err_p999"] = float(np.percentile(diff, 99.9))
        res["err_px"] = int((diff > 16).sum())
        if args.debug:
            debug_image(res, os.path.join(args.debug, f"roles_{face}.png"))

    write_header(args.dst, faces)

    print(f"{'face':10} {'parts':>5} {'stencil':>8} {'table':>6} {'total':>7}   "
          f"{'max err':>7} {'p99.9':>6} {'px>16':>6}   inks (back -> front, * = extra)")
    grand = 0
    for face, res in faces:
        st = sum(len(p["data"]) for p in res["parts"])
        tb = len(res["parts"]) * PART_STRUCT_BYTES
        grand += st + tb
        inks = " ".join(("*" if k in res["extra_inks"] else "") + "#" + to_hex(res["inks"][k])
                        for k in res["order"])
        print(f"{face:10} {len(res['parts']):5d} {st:8d} {tb:6d} {st + tb:7d}   "
              f"{res['err_max']:7d} {res['err_p999']:6.1f} {res['err_px']:6d}   {inks}")
    grand += len(faces) * 12   # face_def_t entries
    print(f"{'TOTAL':10} {'':5} {'':8} {'':6} {grand:7d}   ({grand / 1024:.1f} KB flash)")
    print(f"wrote {os.path.relpath(args.dst)} and {len(faces)} images in {os.path.relpath(check_dir)}")


if __name__ == "__main__":
    main()
