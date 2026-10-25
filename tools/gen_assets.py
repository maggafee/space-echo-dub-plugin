#!/usr/bin/env python3
"""
Builds all GUI bitmaps for the Dubasco Tape Echo plugin from the source
artwork in assets_src/ and writes Source/Layout.h with every control position.

Run:  python3 -I tools/gen_assets.py   (needs Pillow, numpy, opencv-python-headless)

Coordinate system: the plugin's "design space" is DESIGN_W x DESIGN_H pixels.
The top TOPBAR_H pixels are the preset/toolbar strip (drawn in code), the rest
is background.png (the mockup + the extra "dub section" row).
"""
import math, os, random, json
import numpy as np
import cv2
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "assets_src")
OUT = os.path.join(ROOT, "Resources", "images")
FONT_SEMI = os.path.join(ROOT, "Resources", "fonts", "Oswald-SemiBold.ttf")
FONT_MED = os.path.join(ROOT, "Resources", "fonts", "Oswald-Medium.ttf")
os.makedirs(OUT, exist_ok=True)

random.seed(7)
np.random.seed(7)

CREAM = (234, 196, 142)
DARK_TXT = (22, 20, 16)
MOCK_CUT = 978          # mockup rows [0, MOCK_CUT) are used, then the dub row, then the bottom wood
EXT_H = 300
TOPBAR_H = 56
BG_W = 1536
BG_H = MOCK_CUT + EXT_H + (1024 - MOCK_CUT)
DESIGN_W, DESIGN_H = BG_W, BG_H + TOPBAR_H

KNOB_START_DEG, KNOB_END_DEG = -135.0, 135.0


def load(name):
    return Image.open(os.path.join(SRC, name)).convert("RGBA")


def font(size, semi=True):
    return ImageFont.truetype(FONT_SEMI if semi else FONT_MED, size)


def bright_centroid(im, thresh=150):
    a = np.asarray(im).astype(float)
    lum = a[..., :3].mean(-1)
    m = (lum > thresh) & (a[..., 3] > 200)
    ys, xs = np.nonzero(m)
    return xs.mean(), ys.mean()


def crop_square(im, cx, cy, half):
    return im.crop((int(round(cx - half)), int(round(cy - half)), int(round(cx + half)), int(round(cy + half))))


def save(im, name, size=None):
    if size:
        im = im.resize(size, Image.LANCZOS)
    im.save(os.path.join(OUT, name), optimize=True)
    return im


# ----------------------------------------------------------------------------- sprites
layout = {}


def make_round_sprite(src, name, out_px, centre=None):
    im = load(src)
    a = np.asarray(im)[..., 3]
    ys, xs = np.nonzero(a > 128)
    if centre is None:
        cx, cy = (xs.min() + xs.max()) / 2.0, (ys.min() + ys.max()) / 2.0
    else:
        cx, cy = centre
    half = max(cx - xs.min(), xs.max() - cx, cy - ys.min(), ys.max() - cy) + 2
    sq = crop_square(im, cx, cy, half)
    save(sq, name, (out_px, out_px))
    # radius of the visible disc relative to sprite half-size
    return float((xs.max() - xs.min()) / 2.0 / half)


# reels: rotate around the hub
rA = make_round_sprite("reel_a.png", "reel_a.png", 800, (625.0, 617.5))
rB = make_round_sprite("reel_b.png", "reel_b.png", 800, (625.3, 621.3))

knob_fill = {}
for k in ["knob_large", "knob_medium", "knob_small"]:
    im = load(k + ".png")
    a = np.asarray(im)[..., 3]
    ys, xs = np.nonzero(a > 128)
    cx, cy = (xs.min() + xs.max()) / 2.0, (ys.min() + ys.max()) / 2.0
    knob_fill[k] = make_round_sprite(k + ".png", k + ".png", 320, (cx, cy))

# lamp on / off
lamp = load("lamp.png")
a = np.asarray(lamp)[..., 3]
ys, xs = np.nonzero(a > 128)
lcx, lcy = (xs.min() + xs.max()) / 2.0, (ys.min() + ys.max()) / 2.0
half = max(xs.max() - lcx, lcy - ys.min(), ys.max() - lcy, lcx - xs.min()) + 2
lamp_sq = crop_square(lamp, lcx, lcy, half).resize((240, 240), Image.LANCZOS)
lamp_sq.save(os.path.join(OUT, "lamp_on.png"), optimize=True)
arr = np.asarray(lamp_sq).astype(float)
h, w = arr.shape[:2]
yy, xx = np.mgrid[0:h, 0:w]
rr = np.hypot(xx - w / 2, yy - h / 2) / (w / 2)
lens = np.clip((0.70 - rr) / 0.05, 0, 1)            # 1 inside the lens
rgb = arr[..., :3]
grey = rgb.mean(-1, keepdims=True)
off_lens = np.concatenate([grey * 0.42 + 18, grey * 0.22 + 8, grey * 0.12 + 4], -1)   # dark amber glass
rgb2 = rgb * (1 - lens[..., None]) + off_lens * lens[..., None]
off = np.concatenate([rgb2, arr[..., 3:]], -1).clip(0, 255).astype(np.uint8)
Image.fromarray(off, "RGBA").save(os.path.join(OUT, "lamp_off.png"), optimize=True)

# lever switch: split into a static base plate and a rotating lever
lev = load("lever.png")
LPX, LPY = 627.0, 517.0                 # pivot (centre of hub and of base plate)
la = np.asarray(lev).astype(np.uint8).copy()
H, W = la.shape[:2]
yy, xx = np.mgrid[0:H, 0:W]
hub = np.hypot(xx - LPX, yy - LPY) < 150
bat = (np.abs(xx - LPX) < 104) & (yy > LPY) & (yy < 1100)
lever_mask = (hub | bat) & (la[..., 3] > 0)
lever_layer = la.copy()
lever_layer[..., 3] = np.where(lever_mask, la[..., 3], 0)
base = la.copy()
rot = cv2.warpAffine(la, cv2.getRotationMatrix2D((LPX, LPY), 180, 1.0), (W, H), flags=cv2.INTER_LINEAR)
fill = bat & ~hub
base[fill] = rot[fill]
plate_r = 372
outside = np.hypot(xx - LPX, yy - LPY) > plate_r
base[..., 3] = np.where(outside & fill, 0, base[..., 3])
half = 380
Image.fromarray(base, "RGBA").crop((int(LPX - half), int(LPY - half), int(LPX + half), int(LPY + half))).resize((300, 300), Image.LANCZOS).save(os.path.join(OUT, "lever_base.png"), optimize=True)
# lever sprite: square around pivot, big enough for the bat (reaches y ~1093 => 576 px below pivot)
half_l = 590
ll = Image.fromarray(lever_layer, "RGBA")
canvas = Image.new("RGBA", (2 * half_l, 2 * half_l), (0, 0, 0, 0))
canvas.paste(ll.crop((int(LPX - half_l), int(LPY - half_l), int(LPX + half_l), int(LPY + half_l))), (0, 0))
canvas.resize((400, 400), Image.LANCZOS).save(os.path.join(OUT, "lever_arm.png"), optimize=True)
LEVER_BASE_HALF, LEVER_ARM_HALF = half, half_l   # ratio arm/base sprite sizes

# toggle: split into bat (above the collar) and nut
tog = load("toggle.png")
ta = np.asarray(tog).copy()
H, W = ta.shape[:2]
yy, xx = np.mgrid[0:H, 0:W]
TPX, TPY = 627.0, 640.0                # pivot where the bat enters the collar
# only the shaft: a narrow column above the collar (wider bit = the ball end); the nut corners stay on the nut
SHX = 632.0
bat_m = (((yy < 560) & (np.abs(xx - SHX) < 124)) | ((yy >= 560) & (yy < TPY + 10) & (np.abs(xx - SHX - 8) < 74))) & (ta[..., 3] > 0)
bat_m = cv2.dilate(bat_m.astype(np.uint8), np.ones((3, 3), np.uint8)).astype(bool)
bat_l = ta.copy(); bat_l[..., 3] = np.where(bat_m, ta[..., 3], 0)
# the nut behind the shaft is hidden in the artwork: rebuild it by mirroring the visible lower half of the
# collar rings (centred at y = RING_CY), so the "off" position does not show a hole
RING_CY = 655
nut_l = ta.copy()
hole = bat_m & (yy > 455)
my = np.clip(2 * RING_CY - yy, 0, H - 1)
mir = ta[my, xx]
soft = cv2.GaussianBlur(cv2.dilate(hole.astype(np.uint8), np.ones((9, 9), np.uint8)).astype(np.float32), (0, 0), 4)[..., None]
soft = np.where(hole[..., None], 1.0, soft) * (yy > 440)[..., None]
nut_l = (ta.astype(np.float32) * (1 - soft) + mir.astype(np.float32) * soft).clip(0, 255).astype(np.uint8)
nut_l[..., 3] = np.where(bat_m & ~hole, 0, nut_l[..., 3])
half_t = 600
for arrx, nm in [(bat_l, "toggle_bat.png"), (nut_l, "toggle_nut.png")]:
    img = Image.fromarray(arrx, "RGBA")
    cv = Image.new("RGBA", (2 * half_t, 2 * half_t), (0, 0, 0, 0))
    cv.paste(img.crop((int(TPX - half_t), int(TPY - half_t), int(TPX + half_t), int(TPY + half_t))), (0, 0))
    cv.resize((300, 300), Image.LANCZOS).save(os.path.join(OUT, nm), optimize=True)

# jack (used for the input selector plug ring) – just a square sprite
make_round_sprite("jack.png", "jack.png", 160)


# chickenhead pointer knob (ECHO MODE selector), rendered from a height field. Points up, pivot = centre.
def make_chickenhead(out_px=360, N=1080):
    u, v = np.meshgrid(np.linspace(-1, 1, N), np.linspace(-1, 1, N))
    px = 2.0 / N

    def capsule_mask(c1, r1, c2, r2):
        m = np.zeros((N, N), np.uint8)
        to = lambda x, y: (int(round((x + 1) / px)), int(round((y + 1) / px)))
        cv2.circle(m, to(*c1), int(r1 / px), 1, -1, cv2.LINE_8)
        cv2.circle(m, to(*c2), int(r2 / px), 1, -1, cv2.LINE_8)
        # outer tangents of the two circles (both on the x = 0 axis)
        d = c1[1] - c2[1]
        sa = (r1 - r2) / d
        ca = math.sqrt(max(0.0, 1 - sa * sa))
        pts = []
        for sgn in (1, -1):
            pts.append(to(c1[0] + sgn * r1 * ca, c1[1] + r1 * sa))
            pts.append(to(c2[0] + sgn * r2 * ca, c2[1] + r2 * sa))
        cv2.fillPoly(m, [np.array([pts[0], pts[1], pts[3], pts[2]])], 1)
        return m

    ptr = capsule_mask((0.0, 0.20), 0.25, (0.0, -0.86), 0.08)
    rr = np.hypot(u, v)
    disc = (rr < 0.50).astype(np.uint8)
    dp = cv2.distanceTransform(ptr, cv2.DIST_L2, 5) * px
    dd = cv2.distanceTransform(disc, cv2.DIST_L2, 5) * px
    # metal skirt: low cone with a rounded rim
    hb = (0.10 + 0.16 * np.sqrt(np.clip(dd / 0.04, 0, 1)) + 0.08 * np.clip(dd / 0.5, 0, 1)) * disc
    ridge = np.clip(1.0 - np.abs(u) / 0.26, 0, 1)
    hp = (0.40 + 0.45 * np.sqrt(np.clip(dp / 0.07, 0, 1)) + 0.12 * ridge * np.clip(dp / 0.07, 0, 1)) * ptr
    hgt = np.maximum(hb, hp)
    hgt = cv2.GaussianBlur(hgt.astype(np.float32), (0, 0), 1.2)

    # albedo: brushed metal skirt, black bakelite pointer with a cream inlay line
    ang = np.arctan2(v, u)
    brushed = 0.44 + 0.06 * np.sin(rr * 900) * np.random.rand(N, N) + 0.05 * np.sin(ang * 3 + 1.0)
    alb = np.dstack([brushed * 1.0, brushed * 0.96, brushed * 0.90]).astype(np.float32)
    alb[ptr > 0] = [0.05, 0.046, 0.042]
    line = (np.abs(u) < 0.024) & (v < -0.10) & (v > -0.80)
    alb[line] = [0.90, 0.84, 0.70]
    hgt = hgt - 0.015 * cv2.GaussianBlur(line.astype(np.float32), (0, 0), 1.2)
    alb *= (1 + np.random.randn(N, N, 1).astype(np.float32) * 0.03)

    gy, gx = np.gradient(hgt, px)
    k = 0.9
    nrm = np.dstack([-gx * k, -gy * k, np.ones_like(hgt)])
    nrm /= np.linalg.norm(nrm, axis=2, keepdims=True)
    L = np.array([-0.45, -0.65, 0.80]); L /= np.linalg.norm(L)
    Hh = L + np.array([0, 0, 1.0]); Hh /= np.linalg.norm(Hh)
    diff = np.clip((nrm * L).sum(-1), 0, 1)
    nh = np.clip((nrm * Hh).sum(-1), 0, 1)
    isP = (ptr > 0)[..., None]
    spec = np.where(isP[..., 0], 0.75 * nh ** 60 + 0.10 * nh ** 8, 0.35 * nh ** 12)
    rgb = alb * (0.35 + 0.85 * diff[..., None]) + spec[..., None]
    # ambient occlusion: dark ring where the pointer sits on the skirt
    ao = 1 - 0.55 * np.exp(-(np.clip(cv2.distanceTransform(1 - ptr, cv2.DIST_L2, 5) * px, 0, 1)) / 0.025) * (1 - isP[..., 0])
    rgb *= ao[..., None]
    a = np.clip(np.maximum(ptr, disc).astype(np.float32), 0, 1)
    img = np.dstack([np.clip(rgb, 0, 1) * 255, a * 255]).astype(np.uint8)
    im = Image.fromarray(img, "RGBA").resize((out_px, out_px), Image.LANCZOS)
    im.save(os.path.join(OUT, "chickenhead.png"), optimize=True)

make_chickenhead()

# ----------------------------------------------------------------------------- background
mock = load("mockup.png").convert("RGB")
M = np.asarray(mock).copy()


def inpaint_rgb(arr, mask, r=5):
    bgr = cv2.cvtColor(arr, cv2.COLOR_RGB2BGR)
    out = cv2.inpaint(bgr, mask.astype(np.uint8) * 255, r, cv2.INPAINT_TELEA)
    return cv2.cvtColor(out, cv2.COLOR_BGR2RGB)


# 1) remove the VU needle (keeps the pivot housing): normalised convolution over the cream face only
mask = np.zeros(M.shape[:2], np.uint8)
cv2.line(mask, (663, 429), (727, 492), 1, 7)
cv2.fillPoly(mask, [np.array([(719, 470), (749, 485), (749, 493), (716, 493)])], 1)
lum = M.astype(float).mean(-1)
face = (lum > 140) & (mask == 0)
face[:, :600] = False; face[:, 940:] = False; face[:355] = False; face[500:] = False
wgt = cv2.GaussianBlur(face.astype(np.float32), (0, 0), 5)
acc = cv2.GaussianBlur(M.astype(np.float32) * face[..., None], (0, 0), 5)
fillc = acc / np.maximum(wgt[..., None], 1e-4)
gr = np.random.randn(*M.shape[:2])[..., None] * 3
mm = mask.astype(bool)
M[mm] = np.clip(fillc + gr, 0, 255)[mm].astype(np.uint8)
for y in range(466, 496):
    row = np.median(M[y, 684:706].astype(float), 0)
    for x in range(708, 752):
        if mm[y, x]:
            M[y, x] = np.clip(row + np.random.randn(3) * 3, 0, 255).astype(np.uint8)
# redraw the start of the scale arc that the needle crossed
arc_img = Image.fromarray(M); ad = ImageDraw.Draw(arc_img)
pts = [(x, 636.0 - math.sqrt(218.0 ** 2 - (x - 767.0) ** 2)) for x in np.linspace(667, 700, 30)]
ad.line(pts, fill=(28, 26, 22), width=3)
M = np.asarray(arc_img).copy()

# 2) the delay time scale in the mockup is garbled – repaint it
DLY_C = (385.0, 795.0)
H, W = M.shape[:2]
yy, xx = np.mgrid[0:H, 0:W]
rr = np.hypot(xx - DLY_C[0], yy - DLY_C[1])
ang = np.degrees(np.arctan2(xx - DLY_C[0], -(yy - DLY_C[1])))     # 0 = up, +cw
plate = (xx > 280) & (xx < 497) & (yy > 713) & (yy < 915)
band = (rr > 58) & (rr < 122) & plate
not_ms = ~((np.abs(ang) > 160) & (rr > 70))        # leave the "ms" caption at the bottom
g = M.astype(int)
creamish = (g[..., 0] > 95) & (g[..., 0] > g[..., 1] - 5) & (g[..., 0] - g[..., 2] > 35)
m2 = band & not_ms & creamish
m2 = cv2.dilate(m2.astype(np.uint8), np.ones((5, 5), np.uint8)).astype(bool) & band & not_ms
M = inpaint_rgb(M, m2, 6)
# re-add a bit of grain over the repaint
noise = (np.random.randn(*M.shape[:2]) * 5).astype(int)
M = np.where(m2[..., None], np.clip(M.astype(int) + noise[..., None], 0, 255), M).astype(np.uint8)

# 3) clear the painted toggle so the animated one sits on clean panel
mask = np.zeros(M.shape[:2], np.uint8)
cv2.circle(mask, (1405, 846), 33, 1, -1)
M = inpaint_rgb(M, mask, 7)

# 4) the painted tape is crumpled: remove it and lay a clean, straight tape path instead
REEL_C = [((322.0, 272.0), 252.0), ((1207.0, 273.0), 252.0)]
H, W = M.shape[:2]
yy, xx = np.mgrid[0:H, 0:W]
g = M.astype(int)
r_, g_, b_ = g[..., 0], g[..., 1], g[..., 2]
rf = np.maximum(r_, 1).astype(float)
tape_old = (g_ / rf < 0.62) & (b_ / rf < 0.52) & (r_ > 50) & (r_ < 220) & (yy > 430) & (yy < 612) & (xx > 62) & (xx < 1474)
for (c, rad) in REEL_C:
    tape_old &= np.hypot(xx - c[0], yy - c[1]) > rad
n_cc, lab, stats, _ = cv2.connectedComponentsWithStats(tape_old.astype(np.uint8), 8)
tape_old = np.isin(lab, [i for i in range(1, n_cc) if stats[i, cv2.CC_STAT_AREA] >= 60]).astype(np.uint8)
tape_old = cv2.dilate(tape_old, np.ones((7, 7), np.uint8)).astype(bool)
for (c, rad) in REEL_C:
    tape_old &= np.hypot(xx - c[0], yy - c[1]) > rad - 6
M_clean = inpaint_rgb(M, tape_old, 9)
noise = (np.random.randn(H, W) * 4).astype(int)
M_clean = np.where(tape_old[..., None], np.clip(M_clean.astype(int) + noise[..., None], 0, 255), M_clean).astype(np.uint8)

# parts the tape runs behind (posts, tension arm, rollers, head blocks): kept on top of the new tape
occ = np.zeros((H, W), np.uint8)
for (x, y, r) in [(97, 476, 27), (550, 519, 27), (986, 519, 27), (1435, 476, 27),
                  (212, 552, 32), (210, 601, 32), (411, 568, 58), (1123, 568, 58), (1320, 561, 48)]:
    cv2.circle(occ, (x, y), r, 1, -1)
for (x0, y0, x1, y1) in [(86, 476, 114, 566), (538, 519, 565, 606), (973, 519, 1000, 606), (1422, 476, 1449, 566),
                         (180, 552, 244, 601), (600, 532, 937, 606)]:
    cv2.rectangle(occ, (x0, y0), (x1, y1), 1, -1)
occ = cv2.GaussianBlur(occ.astype(np.float32), (0, 0), 0.8)

TAPE_W = 11.0
TAPE_PATH = [(196, 452), (89, 523), (238, 563), (372, 596), (452, 525), (540, 525), (998, 525),
             (1080, 528), (1176, 578), (1276, 578), (1358, 528), (1447, 520), (1338, 452)]
SS = 4
tl = Image.new("L", (W * SS, H * SS), 0)
ImageDraw.Draw(tl).line([(x * SS, y * SS) for x, y in TAPE_PATH], fill=255, width=int(TAPE_W * SS), joint="curve")
tape_a = np.asarray(tl.resize((W, H), Image.LANCZOS)).astype(np.float32) / 255.0
for (c, rad) in REEL_C:
    tape_a *= np.hypot(xx - c[0], yy - c[1]) > rad - 4
# oxide brown with a soft sheen along the length; slight edge darkening
edge = cv2.GaussianBlur(tape_a, (0, 0), 1.6)
tape_rgb = np.array([86, 49, 30], np.float32) * (0.75 + 0.35 * edge[..., None])
sh = np.clip(cv2.GaussianBlur(tape_a, (0, 0), 3.0) - 0.55, 0, 1)
tape_rgb += sh[..., None] * np.array([60, 40, 26], np.float32)
# drop shadow on the panel
shadow = cv2.GaussianBlur(np.roll(np.roll(tape_a, 3, 0), 2, 1), (0, 0), 2.5)
base = M_clean.astype(np.float32) * (1 - 0.45 * shadow[..., None] * (1 - occ[..., None]))
vis = tape_a * (1 - occ)
M = (base * (1 - vis[..., None]) + tape_rgb * vis[..., None]).clip(0, 255).astype(np.uint8)
# visible tape as alpha mask for the animated sheen drawn by the editor
TAPE_BOX = (60, 430, 1476, 640)
tm = (vis[TAPE_BOX[1]:TAPE_BOX[3], TAPE_BOX[0]:TAPE_BOX[2]] * 255).astype(np.uint8)
Image.merge("RGBA", [Image.new("L", (tm.shape[1], tm.shape[0]), 255)] * 3 + [Image.fromarray(tm, "L")]).save(os.path.join(OUT, "tape_mask.png"), optimize=True)

bg = Image.new("RGB", (BG_W, BG_H), (20, 19, 16))
bg.paste(Image.fromarray(M[:MOCK_CUT]), (0, 0))
bg.paste(Image.fromarray(M[MOCK_CUT:]), (0, MOCK_CUT + EXT_H))   # bottom wood rail

# wood side rails for the extension: tile from the mockup's side rails
for (x0, x1) in [(0, 58), (1478, 1536)]:
    strip = Image.fromarray(M[300:300 + EXT_H, x0:x1])
    bg.paste(strip, (x0, MOCK_CUT))
# dark frame of the extension
ext = Image.fromarray(M[MOCK_CUT - EXT_H - 20:MOCK_CUT - 20, 58:1478]).copy()   # placeholder, overwritten below
d = ImageDraw.Draw(bg)
frame_rgb = np.asarray(Image.fromarray(M[960:977, 300:1300]))
fr = np.full((EXT_H, 1478 - 58, 3), (27, 26, 22), np.uint8)
grain = (np.random.randn(EXT_H, 1478 - 58) * 4).astype(int)
fr = np.clip(fr.astype(int) + grain[..., None], 0, 255).astype(np.uint8)
bg.paste(Image.fromarray(fr), (58, MOCK_CUT))
# continue the black lower edge of the mockup row (the strip between rasta stripe and wood) at the seam
d.rectangle((58, MOCK_CUT, 1478, MOCK_CUT + 1), fill=(8, 8, 7))

# value noise helper for plate textures
def value_noise(h, w, cell, amp):
    gh, gw = h // cell + 2, w // cell + 2
    g = np.random.rand(gh, gw).astype(np.float32)
    g = cv2.resize(g, (gw * cell, gh * cell), interpolation=cv2.INTER_CUBIC)[:h, :w]
    return (g - 0.5) * 2 * amp


def plate_texture(w, h, base):
    base = np.array(base, np.float32)
    t = np.ones((h, w, 3), np.float32) * base
    lum = 1 + value_noise(h, w, 60, 0.06) + value_noise(h, w, 12, 0.04) + np.random.randn(h, w) * 0.035
    t *= lum[..., None]
    # dark specks and rust flecks
    im = Image.fromarray(np.clip(t, 0, 255).astype(np.uint8))
    dr = ImageDraw.Draw(im)
    for _ in range(int(w * h / 900)):
        x, y = random.randrange(w), random.randrange(h)
        r = random.choice([0.6, 0.8, 1, 1, 1.5, 2])
        c = random.choice([(10, 8, 6), (25, 18, 12), (120, 70, 25), (160, 110, 50)])
        dr.ellipse((x - r, y - r, x + r, y + r), fill=c)
    for _ in range(int(w * h / 6000)):
        x, y = random.randrange(w), random.randrange(h)
        l = random.randint(6, 26); a = random.uniform(0, math.pi)
        c = tuple(int(min(255, v * 1.35 + 10)) for v in base)
        dr.line((x, y, x + l * math.cos(a), y + l * math.sin(a)), fill=c, width=1)
    arr = np.asarray(im).astype(np.float32)
    # edge wear / vignette
    yy, xx = np.mgrid[0:h, 0:w]
    e = np.minimum.reduce([xx, yy, w - 1 - xx, h - 1 - yy]).astype(np.float32)
    vig = 0.80 + 0.20 * np.clip(e / 28.0, 0, 1)
    arr *= vig[..., None]
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))


screw = Image.fromarray(np.asarray(load("mockup.png"))[671:700, 78:107]).convert("RGBA")
sm = Image.new("L", screw.size, 0)
ImageDraw.Draw(sm).ellipse((1, 1, screw.size[0] - 2, screw.size[1] - 2), fill=255)
screw.putalpha(sm.filter(ImageFilter.GaussianBlur(0.7)))


def rivet(dr, x, y):
    dr.ellipse((x - 4, y - 4, x + 4, y + 4), fill=(12, 11, 9))
    dr.ellipse((x - 2.5, y - 2.5, x + 1.5, y + 1.5), fill=(70, 66, 58))


PLATE_Y0, PLATE_Y1 = MOCK_CUT + 12, MOCK_CUT + EXT_H - 14
COLORS = {"red": (138, 25, 19), "green": (18, 74, 34), "yellow": (205, 128, 20), "black": (30, 29, 25)}


def draw_plate(x0, x1, colour, title):
    w, h = x1 - x0, PLATE_Y1 - PLATE_Y0
    tex = plate_texture(w, h, COLORS[colour])
    # bevel line (thin light border like the mockup plates)
    dr = ImageDraw.Draw(tex)
    edge = tuple(int(min(255, c * 0.5 + 110)) for c in COLORS[colour]) if colour != "black" else (120, 100, 70)
    dr.rounded_rectangle((1, 1, w - 2, h - 2), radius=6, outline=edge, width=2)
    dr.rounded_rectangle((0, 0, w - 1, h - 1), radius=7, outline=(6, 6, 5), width=1)
    for (x, y) in [(10, 10), (w - 11, 10), (10, h - 11), (w - 11, h - 11)]:
        rivet(dr, x, y)
    # shadow under plate
    bg.paste(tex, (x0, PLATE_Y0))
    txt = DARK_TXT if colour == "yellow" else CREAM
    text_center(title, (x0 + x1) / 2, PLATE_Y0 + 26, 27, txt)
    return txt


bgd = ImageDraw.Draw(bg)


def text_center(s, cx, cy, size, fill, semi=True):
    f = font(size, semi)
    l, t, r, b = bgd.textbbox((0, 0), s, font=f)
    bgd.text((cx - (r - l) / 2 - l, cy - (b - t) / 2 - t), s, font=f, fill=fill)


def ticks(cx, cy, r0, r1, n, colour, start=KNOB_START_DEG, end=KNOB_END_DEG, width=2):
    for i in range(n):
        a = math.radians(start + (end - start) * i / (n - 1))
        sx, sy = math.sin(a), -math.cos(a)
        bgd.line((cx + sx * r0, cy + sy * r0, cx + sx * r1, cy + sy * r1), fill=colour, width=width)


# redraw the delay time scale (log 50..600 ms)
DLY_MIN, DLY_MAX = 50.0, 600.0
def dly_norm(ms):
    return math.log(ms / DLY_MIN) / math.log(DLY_MAX / DLY_MIN)

for v in [50, 75, 100, 250, 400, 600]:
    n = dly_norm(v)
    a = math.radians(KNOB_START_DEG + (KNOB_END_DEG - KNOB_START_DEG) * n)
    sx, sy = math.sin(a), -math.cos(a)
    cx, cy = DLY_C
    bgd.line((cx + sx * 64, cy + sy * 64, cx + sx * 76, cy + sy * 76), fill=CREAM, width=3)
    text_center(str(v), cx + sx * 96, cy + sy * 92, 21, CREAM)
for i in range(23):
    n = i / 22
    a = math.radians(KNOB_START_DEG + (KNOB_END_DEG - KNOB_START_DEG) * n)
    sx, sy = math.sin(a), -math.cos(a)
    cx, cy = DLY_C
    bgd.line((cx + sx * 64, cy + sy * 64, cx + sx * 70, cy + sy * 70), fill=CREAM, width=1)

# ---- the dub section
# x ranges of the plates
P = {
    "mode": (70, 314, "red", "ECHO MODE"),
    "spring": (318, 560, "green", "SPRING REVERB"),
    "tape": (564, 894, "yellow", "TAPE"),
    "time": (898, 1136, "black", "TIME · STEREO"),
    "mix": (1140, 1346, "black", "MIX"),
    "dub": (1350, 1468, "black", "DUB"),
}
plate_txt = {}
for k, (x0, x1, c, t) in P.items():
    plate_txt[k] = draw_plate(x0, x1, c, t)

controls = {}
EY = MOCK_CUT  # extension origin


def knob(name, kind, cx, cy, r, label, plate, scale=None, nticks=11):
    col = plate_txt[plate]
    ticks(cx, cy, r + 5, r + 13, nticks, col)
    if label:
        text_center(label, cx, cy + r + 27, 15 if len(label) > 9 else 16, col)
    if scale:
        lo, hi = scale
        a0, a1 = math.radians(KNOB_START_DEG), math.radians(KNOB_END_DEG)
        text_center(lo, cx + math.sin(a0) * (r + 20), cy - math.cos(a0) * (r + 20) + 6, 14, col, False)
        text_center(hi, cx + math.sin(a1) * (r + 20), cy - math.cos(a1) * (r + 20) + 6, 14, col, False)
    controls[name] = {"type": "knob", "sprite": kind, "x": cx, "y": cy, "r": r}


def lever(name, cx, cy, r, positions, labels, plate, label_r=None, label_size=15, caption=None):
    col = plate_txt[plate]
    label_r = label_r or r + 22
    for a, s in zip(positions, labels):
        ar = math.radians(a)
        bgd.line((cx + math.sin(ar) * (r + 3), cy - math.cos(ar) * (r + 3), cx + math.sin(ar) * (r + 9), cy - math.cos(ar) * (r + 9)), fill=col, width=2)
        text_center(s, cx + math.sin(ar) * label_r, cy - math.cos(ar) * label_r, label_size, col)
    if caption:
        text_center(caption, cx, cy + r + 30, 16, col)
    controls[name] = {"type": "lever", "x": cx, "y": cy, "r": r, "angles": positions}


def button(name, cx, cy, r, label, plate, below=True):
    col = plate_txt[plate]
    if label:
        text_center(label, cx, cy + (r + 17 if below else -(r + 15)), 16, col)
    controls[name] = {"type": "button", "x": cx, "y": cy, "r": r}


# ECHO MODE: 12 position rotary selector
mx, my = (P["mode"][0] + P["mode"][1]) / 2, EY + 128
mode_angles = [-150 + 300 * i / 11 for i in range(12)]
lever("mode", mx, my, 42, mode_angles, [str(i + 1) for i in range(12)], "mode", label_r=70, label_size=18)
# head indicator lamps
heads_y = EY + 238
hx = [mx - 72, mx - 30, mx + 12, mx + 66]
for i, s in enumerate(["1", "2", "3", "REV"]):
    controls["headLamp%d" % i] = {"type": "lamp", "x": hx[i], "y": heads_y, "r": 10}
    text_center(s, hx[i], heads_y + 22, 13, CREAM)
text_center("HEADS", mx - 30, heads_y - 22, 12, CREAM, False)

# SPRING REVERB
sx0, sx1 = P["spring"][0], P["spring"][1]
knob("reverb", "knob_medium", sx0 + 66, EY + 112, 38, "REVERB", "spring", ("0", "10"))
knob("springDecay", "knob_small", sx1 - 66, EY + 112, 34, "DECAY", "spring", ("S", "L"))
button("kick", (sx0 + sx1) / 2, EY + 214, 22, None, "spring")
text_center("SPRING", (sx0 + sx1) / 2 - 62, EY + 214, 16, CREAM)
text_center("KICK", (sx0 + sx1) / 2 + 56, EY + 214, 16, CREAM)

# TAPE
tx0, tx1 = P["tape"][0], P["tape"][1]
tc = (tx0 + tx1) / 2
knob("wowFlutter", "knob_medium", tc - 108, EY + 118, 40, "WOW & FLUTTER", "tape")
knob("tapeAge", "knob_medium", tc, EY + 118, 40, "TAPE AGE", "tape")
knob("inertia", "knob_medium", tc + 108, EY + 118, 40, "INERTIA", "tape")
text_center("TAPE SPEED", tc, EY + 236, 13, DARK_TXT, False)
controls["tapeReadout"] = {"type": "readout", "x": tc, "y": EY + 254, "w": 220, "h": 22}

# TIME / STEREO
ix0, ix1 = P["time"][0], P["time"][1]
lever("sync", ix0 + 58, EY + 108, 21, [-45, 45], ["FREE", "SYNC"], "time", label_r=46, label_size=14)
knob("syncNote", "knob_small", ix1 - 64, EY + 102, 30, None, "time")
text_center("NOTE", ix1 - 64, EY + 58, 13, CREAM, False)
controls["noteReadout"] = {"type": "readout", "x": ix1 - 64, "y": EY + 158, "w": 84, "h": 22}
lever("stereoMode", (ix0 + ix1) / 2, EY + 232, 20, [-90, -35, 35, 90], ["MONO", "WIDE", "STEREO", "PING"], "time", label_r=48, label_size=12)
text_center("STEREO MODE", (ix0 + ix1) / 2, EY + 174, 12, CREAM, False)

# MIX
gx0, gx1 = P["mix"][0], P["mix"][1]
knob("lowCut", "knob_small", gx0 + 52, EY + 90, 27, "LOW CUT", "mix")
knob("duck", "knob_small", gx1 - 52, EY + 90, 27, "DUCKING", "mix")
knob("dry", "knob_small", gx0 + 52, EY + 200, 27, "DRY", "mix")
knob("output", "knob_small", gx1 - 52, EY + 200, 27, "OUTPUT", "mix")

# DUB
dx = (P["dub"][0] + P["dub"][1]) / 2
button("throw", dx, EY + 84, 24, "THROW", "dub")
button("hold", dx, EY + 162, 24, "HOLD", "dub")
lever("sendMode", dx, EY + 238, 13, [-50, 50], ["ALL", "THR"], "dub", label_r=33, label_size=12)

# frame screws at the corners of the extension
for (x, y) in [(70, EY + 6), (1466, EY + 6)]:
    pass

bg.save(os.path.join(OUT, "background.jpg"), quality=93)

# ---- the mockup controls (positions measured on the 1536x1024 mockup)
controls.update({
    "input":      {"type": "knob", "sprite": "knob_large", "x": 170, "y": 780, "r": 60},
    "delayTime":  {"type": "knob", "sprite": "knob_large", "x": 385, "y": 795, "r": 61},
    "feedback":   {"type": "knob", "sprite": "knob_large", "x": 595, "y": 786, "r": 64},
    "bass":       {"type": "knob", "sprite": "knob_medium", "x": 775, "y": 787, "r": 60},
    "treble":     {"type": "knob", "sprite": "knob_medium", "x": 942, "y": 787, "r": 59},
    "echoLevel":  {"type": "knob", "sprite": "knob_medium", "x": 1107, "y": 789, "r": 60},
    "tapeSat":    {"type": "knob", "sprite": "knob_small", "x": 1274, "y": 780, "r": 55},
    "power":      {"type": "toggle", "x": 1405, "y": 848, "r": 30},
    "powerLamp":  {"type": "lamp", "x": 1405, "y": 744, "r": 33},
    "jackMic":    {"type": "jack", "x": 133, "y": 891, "r": 25},
    "jackLine":   {"type": "jack", "x": 214, "y": 891, "r": 25},
    "reelL":      {"type": "reel", "x": 322, "y": 272, "r": 248},
    "reelR":      {"type": "reel", "x": 1207, "y": 273, "r": 248},
    "vu":         {"type": "vu", "x": 622, "y": 368, "w": 294, "h": 121, "px": 767, "py": 505},
})

# VU scale: x positions of the marks on the painted scale -> needle angles
arc_c, arc_r = (767.0, 636.0), 218.0
vu_marks = [(-20, 668), (-10, 697), (-7, 711), (-5, 731), (-3, 755), (-1, 781), (0, 797), (1, 811), (3, 840), (5, 867)]
vu_tab = []
for db, x in vu_marks:
    y = arc_c[1] - math.sqrt(arc_r ** 2 - (x - arc_c[0]) ** 2)
    a = math.degrees(math.atan2(x - 767.0, -(y - 505.0)))
    vu_tab.append((db, round(a, 2)))

# sprite meta
meta = {
    "reelFill": [rA, rB],
    "knobFill": knob_fill,
    "leverArmToBase": LEVER_ARM_HALF / LEVER_BASE_HALF,
}

# ----------------------------------------------------------------------------- Layout.h
L = []
L.append("// Generated by tools/gen_assets.py - do not edit by hand.\n#pragma once\n\nnamespace layout\n{")
L.append(f"    constexpr int designW = {DESIGN_W};\n    constexpr int designH = {DESIGN_H};\n    constexpr int topBarH = {TOPBAR_H};")
L.append(f"    constexpr float knobStartDeg = {KNOB_START_DEG}f, knobEndDeg = {KNOB_END_DEG}f;")
L.append(f"    constexpr float leverArmToBase = {meta['leverArmToBase']:.4f}f;")
L.append(f"    constexpr float knobFillLarge = {knob_fill['knob_large']:.4f}f, knobFillMedium = {knob_fill['knob_medium']:.4f}f, knobFillSmall = {knob_fill['knob_small']:.4f}f;")
L.append(f"    constexpr float reelFill = {max(rA, rB):.4f}f;")
L.append("\n    struct Ctl { const char* id; const char* type; const char* sprite; float x, y, r, w, h; };")
L.append("    // positions are in background coordinates (add topBarH for design space)")
L.append("    constexpr Ctl controls[] = {")
for k, c in controls.items():
    L.append('        {{ "{}", "{}", "{}", {:.1f}f, {:.1f}f, {:.1f}f, {:.1f}f, {:.1f}f }},'.format(
        k, c["type"], c.get("sprite", ""), c["x"], c["y"], c.get("r", 0), c.get("w", 0), c.get("h", 0)))
L.append("    };\n")
for k, c in controls.items():
    if c["type"] == "lever":
        L.append("    constexpr float {}Angles[] = {{ {} }};".format(k, ", ".join("%.2ff" % a for a in c["angles"])))
L.append("    constexpr float tapeW = {:.1f}f;".format(TAPE_W))
L.append("    constexpr float tapeBox[] = {{ {} }};".format(", ".join("%.1ff" % v for v in TAPE_BOX)))
L.append("    constexpr float tapePath[][2] = {{ {} }};".format(", ".join("{ %.1ff, %.1ff }" % p for p in TAPE_PATH)))
L.append("    constexpr float vuPivotX = 767.0f, vuPivotY = 505.0f, vuNeedleLen = 118.0f;")
L.append("    constexpr float vuDb[]  = {{ {} }};".format(", ".join("%.1ff" % d for d, _ in vu_tab)))
L.append("    constexpr float vuDeg[] = {{ {} }};".format(", ".join("%.2ff" % a for _, a in vu_tab)))
L.append("}\n")
with open(os.path.join(ROOT, "Source", "Layout.h"), "w") as f:
    f.write("\n".join(L))
print("ok", DESIGN_W, DESIGN_H, json.dumps(meta))
