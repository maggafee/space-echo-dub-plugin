#!/usr/bin/env python3
"""Builds the MPC touch-screen skin (Plugin Skins/) for the Dubasco Tape Echo
from the same artwork the desktop plugin uses (Resources/images, Source/Layout.h).

Every control is pre-rendered onto its own piece of the panel background, so the
film strips and button images are opaque and line up with the page background
pixel for pixel: no halos or cut-out edges around knobs and switches.

Output: mpc/TapeEcho/skins/ (TUI.json, Q-Links.json, images/*.png). tools/build.py
of the MPC Plugin SDK copies that folder verbatim into "Plugin Skins/".

Requires Pillow.  Run from anywhere:  python3 mpc/TapeEcho/tools/make_skin.py
"""
import json
import math
import os
import re
import shutil

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
IMG = os.path.join(REPO, "Resources", "images")
OUT = os.path.join(REPO, "mpc", "TapeEcho", "skins")
OUT_IMG = os.path.join(OUT, "images")

TAB_W, TAB_H = 1280, 628
KNOB_FRAMES = 48
SS = 2  # supersampling for the rotated sprites

# --------------------------------------------------------------------------- parameters
# Index = position in TapeEcho.cpp's enum Params (permanent).
P = dict(output=0, input=1, delayTime=2, feedback=3, echoLevel=4, reverb=5, bass=6, treble=7,
         tapeSat=8, mode=9, sync=10, syncNote=11, stereoMode=12, wowFlutter=13, tapeAge=14,
         inertia=15, springDecay=16, lowCut=17, duck=18, dry=19, inputMode=20, sendMode=21,
         throw=22, hold=23, power=24, kick=25)

CHOICES = {
    "mode": ["1 H1", "2 H2", "3 H3", "4 H2+3", "5 H1+R", "6 H2+R", "7 H3+R",
             "8 H12+R", "9 H23+R", "10 H13+R", "11 All+R", "12 Rev"],
    "sync": ["Off", "On"],
    "syncNote": ["1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D",
                 "1/4T", "1/4", "1/4D", "1/2T", "1/2", "1/2D", "1/1"],
    "stereoMode": ["Mono", "Wide", "Stereo", "PingPong"],
    "inputMode": ["Mic", "Line"],
    "sendMode": ["All", "Throw"],
    "throw": ["Off", "On"], "hold": ["Off", "On"], "power": ["Off", "On"], "kick": ["Off", "On"],
}

# --------------------------------------------------------------------------- layout
def read_layout():
    src = open(os.path.join(REPO, "Source", "Layout.h")).read()
    ctl = {}
    for m in re.finditer(r'\{ "(\w+)", "(\w+)", "(\w*)", ([-\d.]+)f, ([-\d.]+)f, ([-\d.]+)f, ([-\d.]+)f, ([-\d.]+)f \}', src):
        cid, typ, sprite, x, y, r, w, h = m.groups()
        ctl[cid] = dict(type=typ, sprite=sprite, x=float(x), y=float(y), r=float(r), w=float(w), h=float(h))

    def arr(name):
        m = re.search(r'%s\[\] = \{([^}]*)\}' % name, src)
        return [float(v.strip().rstrip("f")) for v in m.group(1).split(",")]

    def scalar(name):
        return float(re.search(r'%s = ([-\d.]+)f' % name, src).group(1))

    lay = dict(controls=ctl,
               modeAngles=arr("modeAngles"), syncAngles=arr("syncAngles"),
               stereoModeAngles=arr("stereoModeAngles"), sendModeAngles=arr("sendModeAngles"),
               knobStart=scalar("knobStartDeg"), knobEnd=scalar("knobEndDeg"),
               armToBase=scalar("leverArmToBase"),
               fill={"knob_large": scalar("knobFillLarge"), "knob_medium": scalar("knobFillMedium"),
                     "knob_small": scalar("knobFillSmall")},
               vu=(scalar("vuPivotX"), scalar("vuPivotY"), scalar("vuNeedleLen")),
               vuDeg=arr("vuDeg"))
    return lay


LAY = read_layout()
SPR = {}


def sprite(name):
    if name not in SPR:
        SPR[name] = Image.open(os.path.join(IMG, name + ".png")).convert("RGBA")
    return SPR[name]


# --------------------------------------------------------------------------- drawing helpers (canvas space)
def paste_rotated(canvas, img, cx, cy, drawn_w, angle_deg, alpha=1.0, sy=1.0):
    """Draws img centred at (cx, cy) with width drawn_w, rotated clockwise by angle_deg.
    sy scales vertically (negative flips), like the desktop BatToggle."""
    aspect = img.height / img.width
    w = max(1, int(round(drawn_w * SS)))
    h = max(1, int(round(drawn_w * aspect * SS * abs(sy))))
    im = img.resize((w, h), Image.LANCZOS)
    if sy < 0:
        im = im.transpose(Image.FLIP_TOP_BOTTOM)
    if angle_deg:
        im = im.rotate(-angle_deg, resample=Image.BICUBIC, expand=True)
    if alpha < 1.0:
        a = im.getchannel("A").point(lambda v: int(v * alpha))
        im.putalpha(a)
    layer = Image.new("RGBA", (canvas.width * SS, canvas.height * SS), (0, 0, 0, 0))
    layer.paste(im, (int(round(cx * SS - im.width / 2)), int(round(cy * SS - im.height / 2))), im)
    layer = layer.resize(canvas.size, Image.LANCZOS)
    canvas.alpha_composite(layer)


def soft_shadow(canvas, cx, cy, r, dy, strength=0.55):
    """Radial black shadow like Controls.cpp softShadow."""
    R = r * 1.18
    size = canvas.size
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    px = layer.load()
    y0, y1 = max(0, int(cy + dy - R)), min(size[1], int(cy + dy + R) + 1)
    x0, x1 = max(0, int(cx - R)), min(size[0], int(cx + R) + 1)
    for y in range(y0, y1):
        for x in range(x0, x1):
            d = math.hypot(x + 0.5 - cx, y + 0.5 - cy - dy) / R
            if d >= 1:
                continue
            if d < 0.72:
                a = strength + (strength * 0.75 - strength) * d / 0.72
            else:
                a = strength * 0.75 * (1 - (d - 0.72) / 0.28)
            px[x, y] = (0, 0, 0, int(255 * a))
    canvas.alpha_composite(layer)


def stroke_line(canvas, p0, p1, width, alpha):
    layer = Image.new("RGBA", (canvas.width * SS, canvas.height * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    w = max(1, int(round(width * SS)))
    a = (p0[0] * SS, p0[1] * SS)
    b = (p1[0] * SS, p1[1] * SS)
    d.line([a, b], fill=(0, 0, 0, int(255 * alpha)), width=w)
    for q in (a, b):
        d.ellipse([q[0] - w / 2, q[1] - w / 2, q[0] + w / 2, q[1] + w / 2], fill=(0, 0, 0, int(255 * alpha)))
    canvas.alpha_composite(layer.resize(canvas.size, Image.LANCZOS))


def glow(canvas, cx, cy, radius, alpha):
    layer = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    px = layer.load()
    for y in range(max(0, int(cy - radius)), min(canvas.height, int(cy + radius) + 1)):
        for x in range(max(0, int(cx - radius)), min(canvas.width, int(cx + radius) + 1)):
            d = math.hypot(x + 0.5 - cx, y + 0.5 - cy) / radius
            if d < 1:
                t = 1 - d
                px[x, y] = (255, int(138 * t + 106 * (1 - t)), 26, int(255 * alpha * t))
    canvas.alpha_composite(layer)


# --------------------------------------------------------------------------- controls (drawn at page scale s)
def draw_knob(canvas, cx, cy, c, prop, s):
    r = c["r"] * s
    soft_shadow(canvas, cx, cy, r, r * 0.08)
    ang = LAY["knobStart"] + prop * (LAY["knobEnd"] - LAY["knobStart"])
    paste_rotated(canvas, sprite(c["sprite"]), cx, cy, 2 * r / LAY["fill"][c["sprite"]], ang)


def draw_lever(canvas, cx, cy, c, angle, s):
    base_w = 2 * c["r"] / 0.98 * s
    soft_shadow(canvas, cx, cy, base_w * 0.48, base_w * 0.05, 0.5)
    paste_rotated(canvas, sprite("lever_base"), cx, cy, base_w, 0)
    a = math.radians(angle)
    L = base_w * 0.55
    stroke_line(canvas, (cx, cy), (cx + math.sin(a) * L + 2 * s, cy - math.cos(a) * L + 4 * s), base_w * 0.12, 0.35)
    paste_rotated(canvas, sprite("lever_arm"), cx, cy, base_w * LAY["armToBase"], angle - 180)


def draw_chickenhead(canvas, cx, cy, angle, s):
    D = 164 * 0.60 * s
    a = math.radians(angle)
    soft_shadow(canvas, cx, cy, D * 0.25, D * 0.04, 0.6)
    stroke_line(canvas, (cx, cy), (cx + math.sin(a) * D * 0.42 + 2 * s, cy - math.cos(a) * D * 0.42 + 4 * s), D * 0.11, 0.32)
    paste_rotated(canvas, sprite("chickenhead"), cx, cy, D, angle)


def draw_toggle(canvas, cx, cy, c, on, s):
    w = 2 * c["r"] / 0.6 * s
    soft_shadow(canvas, cx, cy + w * 0.12, w * 0.22, 0, 0.5)
    paste_rotated(canvas, sprite("toggle_nut"), cx, cy, w, 0)
    if on:
        paste_rotated(canvas, sprite("toggle_bat"), cx, cy, w, 0)
    else:
        # flipped bat (0.82 shorter) with a soft shadow, like BatToggle
        sy = -0.82
        paste_rotated(canvas, sprite("toggle_bat"), cx + 2 * s, cy + 3 * s - (w - w * 0.82) * 0, w, 0, alpha=0.35, sy=sy)
        paste_rotated(canvas, sprite("toggle_bat"), cx, cy, w, 0, sy=sy)


def draw_lamp(canvas, cx, cy, r, brightness, s):
    w = 2 * r * s
    if brightness > 0:
        glow(canvas, cx, cy, w * 0.8, 0.55 * brightness)
    soft_shadow(canvas, cx, cy, w * 0.5, w * 0.05, 0.45)
    paste_rotated(canvas, sprite("lamp_off"), cx, cy, w, 0)
    if brightness > 0:
        paste_rotated(canvas, sprite("lamp_on"), cx, cy, w, 0, alpha=brightness)


def draw_plug(canvas, cx, cy, r):
    """A 1/4" plug in the selected jack (JackSelector::paint)."""
    big = Image.new("RGBA", (canvas.width * SS, canvas.height * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    X, Y, R = cx * SS, cy * SS, r * SS

    def ell(x, y, rr, **kw):
        d.ellipse([x - rr, y - rr, x + rr, y + rr], **kw)

    ell(X + 2 * SS, Y + 4 * SS, R * 0.95, fill=(0, 0, 0, 115))
    steps = 24
    for i in range(steps):      # radial-ish boot gradient
        t = i / (steps - 1)
        col = int(0x3a * (1 - t) + 0x05 * t)
        ell(X - R * 0.1 * (1 - t), Y - R * 0.12 * (1 - t), R * 0.86 * (1 - 0.6 * t), fill=(col, col, col, 255))
    ell(X, Y, R * 0.64, outline=(0x6b, 0x6b, 0x6b, 153), width=max(1, int(R * 0.06)))
    ell(X, Y, R * 0.38, fill=(0x18, 0x18, 0x18, 255))
    d.ellipse([X - R * 0.5, Y - R * 0.62, X, Y - R * 0.34], fill=(255, 255, 255, 46))
    ell(X, Y, R * 1.12, outline=(0xff, 0xb3, 0x47, 140), width=max(1, int(R * 0.07)))
    canvas.alpha_composite(big.resize(canvas.size, Image.LANCZOS))


def draw_vu_needle(canvas, page):
    px, py, L = LAY["vu"]
    ang = math.radians(LAY["vuDeg"][0] - 2)     # needle at rest
    x0, y0 = page.map(px, py)
    x1, y1 = page.map(px + math.sin(ang) * L, py - math.cos(ang) * L)
    big = Image.new("RGBA", (canvas.width * SS, canvas.height * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    d.line([(x0 * SS + 2, y0 * SS + 3), (x1 * SS + 2, y1 * SS + 3)], fill=(0, 0, 0, 70), width=max(1, int(3 * page.s * SS)))
    d.line([(x0 * SS, y0 * SS), (x1 * SS, y1 * SS)], fill=(0x22, 0x16, 0x10, 255), width=max(1, int(2.6 * page.s * SS)))
    canvas.alpha_composite(big.resize(canvas.size, Image.LANCZOS))


def draw_reels(canvas, page):
    for cid, spr, ang in (("reelL", "reel_a", math.degrees(0.3)), ("reelR", "reel_b", math.degrees(1.1))):
        c = LAY["controls"][cid]
        x, y = page.map(c["x"], c["y"])
        r = c["r"] * page.s
        soft_shadow(canvas, x, y, r, r * 0.03, 0.6)
        paste_rotated(canvas, sprite(spr), x, y, 2 * r / 0.9964, ang)


# --------------------------------------------------------------------------- pages
class Page:
    """A view of the 1536 x 1324 panel: crop (y0..y1 of the background), scale s, x offset."""

    def __init__(self, name, y0, y1):
        self.name = name
        self.y0, self.y1 = y0, y1
        bg = Image.open(os.path.join(IMG, "background.jpg")).convert("RGBA")
        crop = bg.crop((0, y0, bg.width, y1))
        self.s = min(TAB_W / crop.width, TAB_H / crop.height)
        w, h = int(round(crop.width * self.s)), int(round(crop.height * self.s))
        self.ox, self.oy = (TAB_W - w) // 2, (TAB_H - h) // 2
        self.img = Image.new("RGBA", (TAB_W, TAB_H), (0x12, 0x0e, 0x0b, 255))
        if self.ox > 0:
            # dark side wings with a soft falloff into the wooden frame
            wing = Image.new("RGBA", (TAB_W, TAB_H), (0, 0, 0, 0))
            dw = ImageDraw.Draw(wing)
            for i in range(24):
                a = int(150 * (1 - i / 24))
                dw.rectangle([self.ox - 24 + i, 0, self.ox - 24 + i, TAB_H], fill=(0, 0, 0, a))
                dw.rectangle([self.ox + w + 23 - i, 0, self.ox + w + 23 - i, TAB_H], fill=(0, 0, 0, a))
            self.img.alpha_composite(wing)
        self.img.paste(crop.resize((w, h), Image.LANCZOS), (self.ox, self.oy))
        self.entries = []
        self.qlinks = {}

    def map(self, x, y):
        return (self.ox + x * self.s, self.oy + (y - self.y0) * self.s)

    def contains(self, c):
        return self.y0 <= c["y"] <= self.y1


def tile_box(cx, cy, half):
    x0 = int(math.floor(cx - half)); y0 = int(math.floor(cy - half))
    x1 = int(math.ceil(cx + half)); y1 = int(math.ceil(cy + half))
    x0 = max(0, x0); y0 = max(0, y0); x1 = min(TAB_W, x1); y1 = min(TAB_H, y1)
    return x0, y0, x1 - x0, y1 - y0


def render_frames(page, box, draw_fn, n):
    """Renders n frames over the page background inside box; returns a vertical strip."""
    x, y, w, h = box
    strip = Image.new("RGBA", (w, h * n))
    for i in range(n):
        canvas = page.img.crop((x, y, x + w, y + h)).copy()
        draw_fn(canvas, i)
        strip.paste(canvas, (0, i * h))
    return strip.convert("RGB")


def save(img, name):
    # Everything is opaque (drawn onto the panel), so JPEG keeps the skin small.
    name = os.path.splitext(name)[0] + ".jpg"
    img.convert("RGB").save(os.path.join(OUT_IMG, name), quality=92, subsampling=0, optimize=True)
    return "images/" + name


# --------------------------------------------------------------------------- TUI.json pieces
def bounds(box, focus="Yes"):
    x, y, w, h = box
    return {"version": 1, "acceptsHWFocus": focus, "showWhenDataModelInvalid": "Hide",
            "whenVisible": "Always", "boundsType": "Absolute", "bounds": "%d %d %d %d" % (x, y, w, h)}


def entry(name, typ, data, box, focus="Yes"):
    return {"version": 2,
            "componentData": {"version": 1, "name": name, "type": typ, "data": data},
            "handle remapping": {"version": 1, "map": []},
            "bounds": bounds(box, focus)}


def h(pid):
    return "Parameter %d" % P[pid]


def add_knob(page, cid, pid=None, frames=None):
    pid = pid or cid
    c = LAY["controls"][cid]
    cx, cy = page.map(c["x"], c["y"])
    box = tile_box(cx, cy, c["r"] * 1.18 * page.s)
    n = frames or KNOB_FRAMES
    lx, ly = box[0], box[1]
    strip = render_frames(page, box, lambda cv, i: draw_knob(cv, cx - lx, cy - ly, c, i / (n - 1), page.s), n)
    img = save(strip, "%s_%s.png" % (page.name.lower(), cid))
    page.entries.append(entry(cid, "Knob", {"version": 3, "knobType": "FilmStrip", "filmStrip": img, "numFrames": n,
                                             "invert": False, "dragOrientation": "Vertical", "handleName": h(pid)}, box))


def add_lever_knob(page, cid, angles, chicken=False):
    """A multi-position lever/selector: film strip with one frame per choice."""
    c = LAY["controls"][cid]
    cx, cy = page.map(c["x"], c["y"])
    half = (82 if chicken else (2 * c["r"] / 0.98) * 0.8) * page.s
    box = tile_box(cx, cy, half)
    lx, ly = box[0], box[1]
    if chicken:
        fn = lambda cv, i: draw_chickenhead(cv, cx - lx, cy - ly, angles[i], page.s)
    else:
        fn = lambda cv, i: draw_lever(cv, cx - lx, cy - ly, c, angles[i], page.s)
    strip = render_frames(page, box, fn, len(angles))
    img = save(strip, "%s_%s.png" % (page.name.lower(), cid))
    page.entries.append(entry(cid, "Knob", {"version": 3, "knobType": "FilmStrip", "filmStrip": img, "numFrames": len(angles),
                                             "invert": False, "dragOrientation": "Horizontal", "handleName": h(cid)}, box))


def add_button(page, name, pid, box, draw_on, draw_off, button_id=1, group=1):
    x, y, w, hh = box
    on = page.img.crop((x, y, x + w, y + hh)).copy(); draw_on(on)
    off = page.img.crop((x, y, x + w, y + hh)).copy(); draw_off(off)
    on_p = save(on.convert("RGB"), "%s_%s_on.png" % (page.name.lower(), name))
    off_p = save(off.convert("RGB"), "%s_%s_off.png" % (page.name.lower(), name))
    page.entries.append(entry(name, "Button", {"version": 2, "onImage": on_p, "offImage": off_p, "buttonId": button_id,
                                                "numButtonsInGroup": group, "handleName": h(pid), "gestureBehaviour": "Instant"}, box))


def add_lever_toggle(page, cid, angles):
    c = LAY["controls"][cid]
    cx, cy = page.map(c["x"], c["y"])
    box = tile_box(cx, cy, (2 * c["r"] / 0.98) * 0.8 * page.s)
    lx, ly = box[0], box[1]
    add_button(page, cid, cid, box,
               lambda cv: draw_lever(cv, cx - lx, cy - ly, c, angles[1], page.s),
               lambda cv: draw_lever(cv, cx - lx, cy - ly, c, angles[0], page.s))


def add_lamp_button(page, cid, lit_when_on=True):
    c = LAY["controls"][cid]
    cx, cy = page.map(c["x"], c["y"])
    box = tile_box(cx, cy, c["r"] * 1.6 * page.s)
    lx, ly = box[0], box[1]
    add_button(page, cid, cid, box,
               lambda cv: draw_lamp(cv, cx - lx, cy - ly, c["r"], 1.0 if lit_when_on else 0.0, page.s),
               lambda cv: draw_lamp(cv, cx - lx, cy - ly, c["r"], 0.0, page.s))


def add_power(page):
    c = LAY["controls"]["power"]
    cx, cy = page.map(c["x"], c["y"])
    box = tile_box(cx, cy, c["r"] / 0.6 * page.s)
    lx, ly = box[0], box[1]
    add_button(page, "power", "power", box,
               lambda cv: draw_toggle(cv, cx - lx, cy - ly, c, True, page.s),
               lambda cv: draw_toggle(cv, cx - lx, cy - ly, c, False, page.s))
    # power lamp follows the switch (2-frame film strip, not touchable)
    lc = LAY["controls"]["powerLamp"]
    lx2, ly2 = page.map(lc["x"], lc["y"])
    lbox = tile_box(lx2, ly2, lc["r"] * 1.6 * page.s)
    strip = render_frames(page, lbox, lambda cv, i: draw_lamp(cv, lx2 - lbox[0], ly2 - lbox[1], lc["r"], float(i), page.s), 2)
    img = save(strip, "%s_powerlamp.png" % page.name.lower())
    page.entries.append(entry("powerLamp", "Image", {"version": 2, "imageType": "FilmStrip", "colour": "0", "image": img,
                                                      "numFrames": 2, "handleName": h("power")}, lbox, focus="No"))


def add_jacks(page):
    for i, cid in enumerate(("jackMic", "jackLine")):
        c = LAY["controls"][cid]
        cx, cy = page.map(c["x"], c["y"])
        box = tile_box(cx, cy, c["r"] * 1.35 * page.s)
        lx, ly = box[0], box[1]
        add_button(page, cid, "inputMode", box,
                   lambda cv, cx=cx, cy=cy, lx=lx, ly=ly, r=c["r"]: draw_plug(cv, cx - lx, cy - ly, r * page.s),
                   lambda cv: None, button_id=i, group=2)


def add_head_lamps(page):
    """Head 1/2/3 + REV indicator lamps under the mode selector, following the echo mode."""
    table = [(1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 1, 1, 0), (1, 0, 0, 1), (0, 1, 0, 1), (0, 0, 1, 1),
             (1, 1, 0, 1), (0, 1, 1, 1), (1, 0, 1, 1), (1, 1, 1, 1), (0, 0, 0, 1)]
    cs = [LAY["controls"]["headLamp%d" % i] for i in range(4)]
    pts = [page.map(c["x"], c["y"]) for c in cs]
    pad = cs[0]["r"] * 1.6 * page.s
    x0 = int(math.floor(min(p[0] for p in pts) - pad)); x1 = int(math.ceil(max(p[0] for p in pts) + pad))
    y0 = int(math.floor(min(p[1] for p in pts) - pad)); y1 = int(math.ceil(max(p[1] for p in pts) + pad))
    box = (x0, y0, x1 - x0, y1 - y0)

    def fn(cv, m):
        for k, (c, p) in enumerate(zip(cs, pts)):
            draw_lamp(cv, p[0] - x0, p[1] - y0, c["r"], float(table[m][k]), page.s)

    strip = render_frames(page, box, fn, 12)
    img = save(strip, "%s_headlamps.png" % page.name.lower())
    page.entries.append(entry("headLamps", "Image", {"version": 2, "imageType": "FilmStrip", "colour": "0", "image": img,
                                                      "numFrames": 12, "handleName": h("mode")}, box, focus="No"))


def add_readout(page, cid, pid):
    c = LAY["controls"][cid]
    cx, cy = page.map(c["x"], c["y"])
    w, hh = c["w"] * page.s, c["h"] * page.s
    box = (int(cx - w / 2), int(cy - hh / 2), int(math.ceil(w)), int(math.ceil(hh)))
    # LCD window drawn into the page background
    d = ImageDraw.Draw(page.img)
    x, y, bw, bh = box
    d.rounded_rectangle([x, y, x + bw, y + bh], radius=bh * 0.2, fill=(0, 0, 0, 255))
    d.rounded_rectangle([x + 1, y + 1, x + bw - 1, y + bh - 1], radius=bh * 0.18, fill=(0x1a, 0x12, 0x0a, 255), outline=(0x4a, 0x3a, 0x24, 255))
    style = {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": "SemiBold", "height": round(bh * 0.8, 1)},
             "colour": "ffffb347", "justification": "horizontallyCentred verticallyCentred", "case": "Original"}
    page.entries.append(entry(cid, "Label", {"version": 1, "textStyle": style, "type": "Value", "handleName": h(pid)}, box, focus="No"))


MAIN_ROW_KNOBS = ["input", "delayTime", "feedback", "bass", "treble", "echoLevel", "tapeSat"]


def build_main_row(page):
    for k in MAIN_ROW_KNOBS:
        add_knob(page, k)
    add_power(page)
    add_jacks(page)


def build_pages():
    # Page 1: the whole machine down to the main control row
    deck = Page("DECK", 0, 985)
    draw_reels(deck.img, deck)
    draw_vu_needle(deck.img, deck)
    build_main_row(deck)

    # Page 2: main row + the dub section
    dub = Page("DUB", 1324 - 754, 1324)
    dub_bg_ready = dub
    add_readout(dub_bg_ready, "tapeReadout", "delayTime")     # drawn into the background first
    add_readout(dub_bg_ready, "noteReadout", "syncNote")
    readouts = dub.entries[:]
    dub.entries = []
    build_main_row(dub)
    add_lever_knob(dub, "mode", LAY["modeAngles"], chicken=True)
    add_head_lamps(dub)
    for k in ("reverb", "springDecay", "wowFlutter", "tapeAge", "inertia", "lowCut", "duck", "dry", "output"):
        add_knob(dub, k)
    add_knob(dub, "syncNote", frames=len(CHOICES["syncNote"]))
    add_lever_toggle(dub, "sync", LAY["syncAngles"])
    add_lever_knob(dub, "stereoMode", LAY["stereoModeAngles"])
    add_lever_toggle(dub, "sendMode", LAY["sendModeAngles"])
    add_lamp_button(dub, "kick", lit_when_on=False)    # every press knocks the springs
    add_lamp_button(dub, "throw")
    add_lamp_button(dub, "hold")
    dub.entries += readouts

    # Q-Links (4 x 4, numbered from the bottom left; rows read top-down)
    deck.qlinks = {13: "input", 14: "delayTime", 15: "feedback", 16: "echoLevel",
                   9: "bass", 10: "treble", 11: "tapeSat", 12: "power",
                   5: "inputMode"}
    dub.qlinks = {13: "mode", 14: "reverb", 15: "springDecay", 16: "wowFlutter",
                  9: "tapeAge", 10: "inertia", 11: "syncNote", 12: "lowCut",
                  5: "duck", 6: "dry", 7: "output", 8: "stereoMode",
                  1: "sync", 2: "kick", 3: "throw", 4: "hold"}
    return [deck, dub]


def write_skin(pages):
    tabs = []
    for i, page in enumerate(pages):
        bg = save(page.img.convert("RGB"), "%s_bg.png" % page.name.lower())
        root = {"version": 2, "actions": [],
                "backgroundData": {"version": 1,
                                   "focussed": {"version": 1, "colour": "ff120e0b", "image": ""},
                                   "unfocussed": {"version": 1, "colour": "ff120e0b", "image": ""}},
                "ignoreMousePresses": False, "disableCoarseDataWheel": False,
                "componentsData": [entry("background", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": bg},
                                         (0, 0, TAB_W, TAB_H), focus="No")] + page.entries}
        tabs.append({"version": 1, "tabName": page.name, "fnKeyIndex": i, "fnKeySubIndex": 0,
                     "componentDefinition": root, "qlinkBoundsData": []})

    tui = {"pageData": {"version": 1,
                        "componentDefinitions": {"version": 2, "importFiles": [], "localComponentDefinitions": []},
                        "replacementStrings": [{"key": h(k), "value": v} for k, v in
                                               sorted(CHOICES.items(), key=lambda kv: P[kv[0]])],
                        "info": {"version": 1, "type": "CompleteDescription"},
                        "tabs": tabs}}
    with open(os.path.join(OUT, "TUI.json"), "w") as f:
        json.dump(tui, f, indent=1)

    def qmap(d):
        return {"Q-Link %d" % n: (P[d[n]] if n in d else -1) for n in range(1, 17)}

    program = ["delayTime", "feedback", "echoLevel", "reverb", "input", "bass", "treble", "tapeSat",
               "mode", "wowFlutter", "tapeAge", "inertia", "springDecay", "lowCut", "duck", "output"]
    ql = {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
          "Screen Mode Q-Links": {"version": 4, "map": [
              {"Tab": i + 1, "SubTab": 1, "Bank Direction": "RowTopDown", "Q-Links": qmap(p.qlinks)}
              for i, p in enumerate(pages)]},
          "Program Mode Q-Links": {"Q-Link %d" % (n + 1): P[pid] for n, pid in enumerate(program)}}
    with open(os.path.join(OUT, "Q-Links.json"), "w") as f:
        json.dump(ql, f, indent=1)


def main():
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT_IMG)
    pages = build_pages()
    write_skin(pages)
    total = sum(os.path.getsize(os.path.join(OUT_IMG, f)) for f in os.listdir(OUT_IMG))
    print("skin written to %s (%d images, %.1f MB)" % (OUT, len(os.listdir(OUT_IMG)), total / 1e6))


if __name__ == "__main__":
    main()
