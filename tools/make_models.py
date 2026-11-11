#!/usr/bin/env python3
"""Generates the game's 3D models as glTF binary files (.glb) in assets/models/.

The game loads these files at startup (src/render/models.cpp); it contains no
model geometry itself. Later, any model can be replaced by one made in
Blender: export it as .glb with the same name, origin and size conventions.

Conventions for every model:
  - meters, Y up, origin at the center of the base (on the ground),
    except cube.glb and crate.glb which are centered;
  - one mesh, triangles with POSITION, NORMAL and COLOR_0 (RGBA);
  - material alphaMode BLEND = drawn transparent, doubleSided = no culling.

Unit sizes (the game scales each copy, see src/render/scene.cpp):
  tree, pine             a full tree at scale 1 (about 4.4 m high)
  rock0..3, stump, bush  fit [-1, 1] x [0, 1] x [-1, 1]
  log                    lies along X, fits [-1, 1] x [0, 2] x [-1, 1]
  cube, crate            fit [-1, 1]^3
  grass_short/tall       one tuft, about 0.35 m / 0.9 m high
  flower                 about 0.35 m high, white petals (tinted by the game)
  cloud0..2              fit [-1, 1] x [0, 1] x [-1, 1]
  mountain0..1           fit [-1, 1] x [0, 1] x [-1, 1]
  shadow                 soft black disc of radius 1 on y = 0
  house_*, fence, sign,  real size (see each function), door / front at -Z;
  well, barrel, lamp,    collision boxes in src/core/world.cpp must match
  tower, ruin_*, pillar, (pillar: unit height, scaled by the game)
  fallen_pillar, rubble,
  rune_stone
  sparkle                small transparent spark (radius 1)
  smoke, ripple          transparent puff (radius 1) and water ring (radius 1)

Character parts (player_*, companion_*, slime_*, mushroom_*, bird_*, eye) are
real-size (meters) and have their origin at their joint, so the game can
rotate them there: legs and arms hang down from the hip / shoulder, heads sit
on the neck, ears and tails grow from their base, wings stick out along +X.
Characters face -Z. The game assembles and animates them in
src/render/characters.cpp.

Usage:
  python3 tools/make_models.py            write assets/models/*.glb
  python3 tools/make_models.py --check    exit 1 if the files are not up to date
Standard library only, and deterministic: same output on every run.
"""
import argparse
import json
import math
import pathlib
import struct
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "assets" / "models"
TAU = 2 * math.pi

# ---------------------------------------------------------------- vectors

def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def mulv(a, b): return (a[0] * b[0], a[1] * b[1], a[2] * b[2])
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def length(a): return math.sqrt(dot(a, a))
def normalize(a):
    n = length(a)
    return (a[0] / n, a[1] / n, a[2] / n) if n > 1e-12 else (0.0, 1.0, 0.0)


MASK = (1 << 64) - 1


def hash01(a, b=0):
    """Deterministic pseudo-random number in [0, 1) (splitmix64)."""
    z = (a * 0x9E3779B97F4A7C15 + b + 0x632BE59BD9B4E019) & MASK
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK
    z ^= z >> 31
    return (z >> 40) / float(1 << 24)


def shade(c, k):
    return (c[0] * k, c[1] * k, c[2] * k, c[3])


def mix(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(4))


# ---------------------------------------------------------------- colors (RGBA)

BARK = (0.42, 0.28, 0.17, 1)
WOOD = (0.80, 0.66, 0.45, 1)
LEAVES = (0.34, 0.62, 0.26, 1)
NEEDLES = (0.18, 0.42, 0.27, 1)
BUSH_LEAVES = (0.40, 0.66, 0.30, 1)
ROCK = (0.56, 0.56, 0.58, 1)
MOSS = (0.38, 0.52, 0.28, 1)
CRATE_PLANK = (0.74, 0.55, 0.32, 1)
CRATE_FRAME = (0.50, 0.34, 0.18, 1)
WHITE = (1, 1, 1, 1)
GRASS_BASE = (0.22, 0.42, 0.16, 1)
GRASS_TIP = (0.58, 0.78, 0.34, 1)
STEM = (0.30, 0.52, 0.22, 1)
FLOWER_HEART = (0.98, 0.82, 0.25, 1)
CLOUD = (1.0, 1.0, 1.0, 1)
CLOUD_UNDER = (0.80, 0.84, 0.90, 1)
MOUNTAIN_GRASS = (0.36, 0.48, 0.32, 1)
MOUNTAIN_ROCK = (0.50, 0.48, 0.50, 1)
SNOW = (0.95, 0.96, 0.99, 1)


# ---------------------------------------------------------------- mesh builder

class Mesh:
    """Flat-shaded triangles (no shared vertices)."""

    def __init__(self, transparent=False, double_sided=False):
        self.positions, self.normals, self.colors = [], [], []
        self.transparent = transparent
        self.double_sided = double_sided

    def tri(self, a, b, c, color, colors=None, normal=None):
        n = cross(sub(b, a), sub(c, a))
        if length(n) < 1e-9:
            return  # degenerate, e.g. the tip of a cone
        n = normal or normalize(n)
        for i, p in enumerate((a, b, c)):
            self.positions.append(p)
            self.normals.append(n)
            self.colors.append(colors[i] if colors else color)

    def quad(self, a, b, c, d, color):
        """Counter-clockwise when seen from the front."""
        self.tri(a, b, c, color)
        self.tri(a, c, d, color)

    def quad_facing(self, a, b, c, d, color, outward):
        """A quad whose front faces the `outward` direction, whatever the vertex order."""
        if dot(cross(sub(b, a), sub(c, a)), outward) < 0:
            a, b, c, d = a, d, c, b
        self.quad(a, b, c, d, color)

    def recompute_normals(self):
        for i in range(0, len(self.positions), 3):
            a, b, c = self.positions[i:i + 3]
            n = normalize(cross(sub(b, a), sub(c, a)))
            self.normals[i:i + 3] = [n, n, n]

    def fit(self, lo, hi):
        """Rescales the mesh so its bounds are exactly [lo, hi]."""
        mn = [min(p[k] for p in self.positions) for k in range(3)]
        mx = [max(p[k] for p in self.positions) for k in range(3)]
        self.positions = [tuple(lo[k] + (p[k] - mn[k]) / (mx[k] - mn[k]) * (hi[k] - lo[k]) for k in range(3))
                          for p in self.positions]
        self.recompute_normals()


def frustum(m, transform, r0, r1, height, segments, side, bottom_cap=None, top_cap=None, phase=0.0):
    """Truncated cone along +Y from y = 0 to `height` (a cone when r1 = 0)."""
    t = transform
    for i in range(segments):
        a0 = phase + TAU * i / segments
        a1 = phase + TAU * (i + 1) / segments
        b0, b1 = (r0 * math.cos(a0), 0, r0 * math.sin(a0)), (r0 * math.cos(a1), 0, r0 * math.sin(a1))
        t0, t1 = (r1 * math.cos(a0), height, r1 * math.sin(a0)), (r1 * math.cos(a1), height, r1 * math.sin(a1))
        m.quad(t(b0), t(t0), t(t1), t(b1), side)
        if top_cap and r1 > 0:
            m.tri(t((0, height, 0)), t(t1), t(t0), top_cap)
        if bottom_cap:
            m.tri(t((0, 0, 0)), t(b0), t(b1), bottom_cap)


def identity(p):
    return p


def icosphere():
    """Unit icosphere with one subdivision (80 faces): vertices, faces."""
    t = (1 + math.sqrt(5)) / 2
    verts = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t),
             (0, -1, -t), (0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    base = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4),
            (11, 10, 2), (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8),
            (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    cache = {}

    def midpoint(a, b):
        key = (min(a, b), max(a, b))
        if key not in cache:
            verts.append(mul(add(verts[a], verts[b]), 0.5))
            cache[key] = len(verts) - 1
        return cache[key]

    faces = []
    for a, b, c in base:
        ab, bc, ca = midpoint(a, b), midpoint(b, c), midpoint(c, a)
        faces += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
    return [normalize(v) for v in verts], faces


def blob(m, center, radii, noise, seed, face_color, reshape=None):
    """A lumpy ball: icosphere with each vertex pushed in or out by up to `noise`."""
    verts, faces = icosphere()
    placed = []
    for i, v in enumerate(verts):
        k = 1 + noise * (2 * hash01(seed, i) - 1)
        p = mulv(mul(v, k), radii)
        if reshape:
            p = reshape(p)
        placed.append(add(center, p))
    for f, (ia, ib, ic) in enumerate(faces):
        a, b, c = placed[ia], placed[ib], placed[ic]
        n = cross(sub(b, a), sub(c, a))
        centroid = mul(add(add(a, b), c), 1 / 3)
        if dot(n, sub(centroid, center)) < 0:  # keep every face pointing outwards
            b, c = c, b
            n = mul(n, -1)
        m.tri(a, b, c, face_color(normalize(n), f))


def jitter(base, seed, amount=0.08):
    return lambda n, face: shade(base, 1 + amount * (2 * hash01(seed, face) - 1))


# ---------------------------------------------------------------- models

def cube():
    m = Mesh()
    c = [(-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1), (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]
    for f in [(1, 2, 6, 5), (4, 7, 3, 0), (3, 7, 6, 2), (4, 0, 1, 5), (5, 6, 7, 4), (0, 3, 2, 1)]:
        m.quad(c[f[0]], c[f[1]], c[f[2]], c[f[3]], WHITE)
    return m


def crate():
    m = Mesh()
    border = 0.18
    for n in [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]:
        u = (1, 0, 0) if abs(n[1]) > 0.5 else (0, 1, 0)
        v = cross(n, u)
        u = cross(v, n)

        def p(s, t, n=n, u=u, v=v):
            return add(add(n, mul(u, s)), mul(v, t))
        o, i = 1.0, 1.0 - border
        m.quad(p(-o, -o), p(o, -o), p(o, -i), p(-o, -i), CRATE_FRAME)
        m.quad(p(-o, i), p(o, i), p(o, o), p(-o, o), CRATE_FRAME)
        m.quad(p(-o, -i), p(-i, -i), p(-i, i), p(-o, i), CRATE_FRAME)
        m.quad(p(i, -i), p(o, -i), p(o, i), p(i, i), CRATE_FRAME)
        m.quad(p(-i, -i), p(i, -i), p(i, i), p(-i, i), CRATE_PLANK)
    return m


def tree():
    m = Mesh()
    frustum(m, identity, 0.2, 0.13, 2.3, 6, BARK)
    crowns = [((0, 2.75, 0), (1.3, 1.05, 1.3)), ((0.6, 2.45, 0.35), (0.85, 0.75, 0.85)),
              ((-0.55, 2.5, -0.3), (0.9, 0.8, 0.9)), ((0.1, 3.55, -0.1), (0.8, 0.7, 0.8))]
    for k, (center, radii) in enumerate(crowns):
        seed = 11 + k
        blob(m, center, radii, 0.12, seed, jitter(LEAVES, seed))
    return m


def pine():
    m = Mesh()
    frustum(m, identity, 0.18, 0.12, 1.3, 6, BARK)
    under = shade(NEEDLES, 0.7)
    tiers = [(0.8, 1.35, 1.9, NEEDLES), (1.8, 1.05, 1.7, shade(NEEDLES, 1.07)), (2.8, 0.7, 1.6, shade(NEEDLES, 1.14))]
    for k, (y, radius, height, color) in enumerate(tiers):
        frustum(m, lambda p, y=y: (p[0], p[1] + y, p[2]), radius, 0, height, 8, color,
                bottom_cap=under, phase=0.4 * k)
    return m


def rock(variant):
    m = Mesh()
    base = shade(ROCK, 0.9 + 0.2 * hash01(variant, 99))
    mossy = variant % 2 == 1

    def color(n, face):
        if mossy and n[1] > 0.75:
            return shade(MOSS, 0.95 + 0.1 * hash01(variant, face))
        return shade(base, 0.9 + 0.18 * hash01(variant + 7, face))
    blob(m, (0, 0, 0), (1, 1, 1), 0.32, 1000 + variant, color, lambda p: (p[0], max(p[1], -0.3), p[2]))
    m.fit((-1, 0, -1), (1, 1, 1))
    return m


def log():
    m = Mesh()
    # Built along Y (0..2, radius 1), then laid down along X, bottom on the ground.
    frustum(m, lambda p: (p[1] - 1, 1 - p[0], p[2]), 1, 1, 2, 8, BARK, bottom_cap=WOOD, top_cap=WOOD)
    return m


def stump():
    m = Mesh()
    frustum(m, identity, 1, 0.88, 1, 8, BARK, top_cap=WOOD)
    return m


def bush():
    m = Mesh()
    parts = [((0, 0.45, 0), (0.75, 0.5, 0.75)), ((0.4, 0.38, 0.25), (0.55, 0.42, 0.55)),
             ((-0.4, 0.4, -0.2), (0.55, 0.45, 0.55)), ((0.1, 0.6, -0.35), (0.45, 0.4, 0.45))]
    for k, (center, radii) in enumerate(parts):
        seed = 50 + k
        blob(m, center, radii, 0.15, seed, jitter(BUSH_LEAVES, seed, 0.1), lambda p: (p[0], max(p[1], -0.35), p[2]))
    m.fit((-1, 0, -1), (1, 1, 1))
    return m


def grass(blades, min_height, max_height, spread, seed):
    """A tuft of thin, slightly curved blades. Lit like the ground (normals up)."""
    m = Mesh(double_sided=True)
    up = (0.0, 1.0, 0.0)
    for b in range(blades):
        angle = TAU * hash01(seed, b * 5)
        dist = spread * math.sqrt(hash01(seed, b * 5 + 1))
        height = min_height + (max_height - min_height) * hash01(seed, b * 5 + 2)
        facing = TAU * hash01(seed, b * 5 + 3)
        lean = 0.35 * height * hash01(seed, b * 5 + 4)
        base = (dist * math.cos(angle), 0.0, dist * math.sin(angle))
        side = (math.cos(facing), 0.0, math.sin(facing))
        out = (-side[2], 0.0, side[0])
        width = 0.035 + 0.02 * hash01(seed, b * 5 + 2)
        mid = add(add(base, (0, height * 0.55, 0)), mul(out, lean * 0.35))
        tip = add(add(base, (0, height, 0)), mul(out, lean))
        b0, b1 = add(base, mul(side, -width)), add(base, mul(side, width))
        m0, m1 = add(mid, mul(side, -width * 0.7)), add(mid, mul(side, width * 0.7))
        middle = mix(GRASS_BASE, GRASS_TIP, 0.55)
        m.tri(b0, b1, m1, None, colors=[GRASS_BASE, GRASS_BASE, middle], normal=up)
        m.tri(b0, m1, m0, None, colors=[GRASS_BASE, middle, middle], normal=up)
        m.tri(m0, m1, tip, None, colors=[middle, middle, GRASS_TIP], normal=up)
    return m


def flower():
    m = Mesh(double_sided=True)
    up = (0.0, 1.0, 0.0)
    h, w = 0.32, 0.012
    m.quad((-w, 0, 0), (w, 0, 0), (w * 0.6, h, 0), (-w * 0.6, h, 0), STEM)
    m.quad((0, 0, -w), (0, 0, w), (0, h, w * 0.6), (0, h, -w * 0.6), STEM)
    for i in range(5):  # petals around a yellow heart, facing up and slightly cupped
        a0, a1 = TAU * i / 5, TAU * (i + 0.5) / 5
        a2 = TAU * (i + 1) / 5
        p0 = (0.03 * math.cos(a0), h + 0.01, 0.03 * math.sin(a0))
        p1 = (0.075 * math.cos(a1), h + 0.025, 0.075 * math.sin(a1))
        p2 = (0.03 * math.cos(a2), h + 0.01, 0.03 * math.sin(a2))
        m.tri((0, h + 0.012, 0), p2, p1, WHITE, normal=up)
        m.tri((0, h + 0.012, 0), p2, p0, FLOWER_HEART, normal=up)
    return m


def cloud(variant):
    m = Mesh()
    count = 5 + variant
    for k in range(count):
        seed = 300 + variant * 20 + k
        x = -1 + 2 * (k + 0.5) / count + 0.2 * (hash01(seed, 1) - 0.5)
        z = 0.5 * (hash01(seed, 2) - 0.5)
        r = 0.35 + 0.3 * hash01(seed, 3) * (1 - abs(x) * 0.5)
        # White on top, slightly grey-blue underneath.
        blob(m, (x, r * 0.55, z), (r, r * 0.65, r * 0.8), 0.1, seed,
             lambda n, face: mix(CLOUD_UNDER, CLOUD, min(1.0, max(0.0, n[1] * 0.8 + 0.6))),
             lambda p: (p[0], max(p[1], -0.2), p[2]))
    m.fit((-1, 0, -1), (1, 1, 1))
    return m


def mountain(variant):
    """Rings of noisy heights around a peak; colored by altitude: grass, rock, snow."""
    m = Mesh()
    rings, segments = 7, 16
    seed = 500 + variant * 50

    def point(ring, seg):
        if ring == 0:
            return (0.0, 1.0, 0.0)
        r = ring / rings
        a = TAU * (seg % segments) / segments
        n = hash01(seed, ring * 100 + seg % segments)
        radius = r * (0.85 + 0.3 * n)
        height = (1 - r) ** 1.4 * (0.9 + 0.2 * hash01(seed + 1, ring * 100 + seg % segments))
        if ring == rings:
            height = 0.0
        return (radius * math.cos(a), height, radius * math.sin(a))

    def color(a, b, c):
        y = (a[1] + b[1] + c[1]) / 3 + 0.08 * (hash01(seed + 2, int((a[0] + 2) * 1000)) - 0.5)
        if y > 0.72:
            return SNOW
        if y > 0.3:
            return shade(MOUNTAIN_ROCK, 0.92 + 0.16 * hash01(seed + 3, int((a[2] + 2) * 1000)))
        return shade(MOUNTAIN_GRASS, 0.92 + 0.16 * hash01(seed + 4, int((a[2] + 2) * 1000)))

    for ring in range(rings):
        for seg in range(segments):
            a, b = point(ring, seg), point(ring, seg + 1)
            c, d = point(ring + 1, seg), point(ring + 1, seg + 1)
            if ring == 0:
                m.tri(a, d, c, color(a, d, c))
            else:
                m.tri(a, b, d, color(a, b, d))
                m.tri(a, d, c, color(a, d, c))
    # Faces must point outwards and up: fix any that ended up reversed.
    for i in range(0, len(m.positions), 3):
        a, b, c = m.positions[i:i + 3]
        n = cross(sub(b, a), sub(c, a))
        if n[1] < 0:
            m.positions[i + 1], m.positions[i + 2] = c, b
    m.fit((-1, 0, -1), (1, 1, 1))
    return m


def shadow():
    """Soft blob shadow: dark center fading to transparent at radius 1."""
    m = Mesh(transparent=True)
    segments = 20
    center, inner, outer = (0, 0, 0, 0.42), (0, 0, 0, 0.3), (0, 0, 0, 0.0)
    up = (0.0, 1.0, 0.0)
    for i in range(segments):
        a0, a1 = TAU * i / segments, TAU * (i + 1) / segments
        i0, i1 = (0.55 * math.cos(a0), 0, 0.55 * math.sin(a0)), (0.55 * math.cos(a1), 0, 0.55 * math.sin(a1))
        o0, o1 = (math.cos(a0), 0, math.sin(a0)), (math.cos(a1), 0, math.sin(a1))
        m.tri((0, 0, 0), i1, i0, None, colors=[center, inner, inner], normal=up)
        m.tri(i0, i1, o1, None, colors=[inner, inner, outer], normal=up)
        m.tri(i0, o1, o0, None, colors=[inner, outer, outer], normal=up)
    return m


# ---------------------------------------------------------------- characters

SKIN = (0.96, 0.78, 0.62, 1)
CHEEK = (0.98, 0.62, 0.58, 1)
HAIR = (0.36, 0.22, 0.12, 1)
ROBE = (0.24, 0.30, 0.68, 1)        # the magician's robe and hat
ROBE_DARK = (0.17, 0.20, 0.48, 1)
GOLD = (0.96, 0.78, 0.30, 1)
STAR = (1.0, 0.92, 0.55, 1)
PANTS = (0.26, 0.22, 0.30, 1)
BOOT = (0.30, 0.20, 0.13, 1)
STAFF_WOOD = (0.48, 0.32, 0.20, 1)
CRYSTAL = (0.62, 0.90, 1.0, 1)
EYE_BLACK = (0.07, 0.07, 0.10, 1)
FUR = (0.95, 0.55, 0.20, 1)
CREAM = (0.99, 0.92, 0.80, 1)
PAW = (0.30, 0.20, 0.16, 1)
NOSE = (0.15, 0.10, 0.10, 1)
SLIME = (0.36, 0.82, 0.70, 1)
CAP_RED = (0.88, 0.24, 0.22, 1)
STEM = (0.96, 0.91, 0.80, 1)
FEET = (0.42, 0.30, 0.22, 1)
FEATHER = (0.99, 0.82, 0.26, 1)
BELLY = (1.0, 0.95, 0.72, 1)
BEAK = (0.98, 0.55, 0.15, 1)


def solid(color):
    return lambda n, face: color


def translate(dx, dy, dz):
    return lambda p: (p[0] + dx, p[1] + dy, p[2] + dz)


def box(m, center, half, color):
    cx, cy, cz = center
    hx, hy, hz = half
    c = [(cx - hx, cy - hy, cz - hz), (cx + hx, cy - hy, cz - hz), (cx + hx, cy + hy, cz - hz), (cx - hx, cy + hy, cz - hz),
         (cx - hx, cy - hy, cz + hz), (cx + hx, cy - hy, cz + hz), (cx + hx, cy + hy, cz + hz), (cx - hx, cy + hy, cz + hz)]
    for f in [(1, 2, 6, 5), (4, 7, 3, 0), (3, 7, 6, 2), (4, 0, 1, 5), (5, 6, 7, 4), (0, 3, 2, 1)]:
        m.quad(c[f[0]], c[f[1]], c[f[2]], c[f[3]], color)


def eye():
    """Shared eye: a black, slightly flat ball (radius 1) with a white highlight."""
    m = Mesh()
    light = normalize((0.45, 0.55, -0.7))
    blob(m, (0, 0, 0), (1, 1, 0.6), 0.0, 900,
         lambda n, face: WHITE if dot(n, light) > 0.86 else EYE_BLACK)
    return m


def star(m, center, radius, facing, color, points=5):
    """A flat five-pointed star on a surface facing `facing` (unit axis vector)."""
    f = normalize(facing)
    up = (0.0, 1.0, 0.0) if abs(f[1]) < 0.9 else (0.0, 0.0, 1.0)
    u = normalize(cross(up, f))
    v = cross(f, u)
    c = add(center, mul(f, 0.004))
    corner = []
    for i in range(2 * points):
        a = math.pi / 2 + math.pi * i / points
        r = radius if i % 2 == 0 else radius * 0.42
        corner.append(add(c, add(mul(u, r * math.cos(a)), mul(v, r * math.sin(a)))))
    for i in range(2 * points):
        a, b = corner[i], corner[(i + 1) % (2 * points)]
        if dot(cross(sub(a, c), sub(b, c)), f) < 0:
            a, b = b, a
        m.tri(c, a, b, color)


def player_leg():
    """Trousers and boots, under the robe."""
    m = Mesh()
    frustum(m, translate(0, -0.30, 0), 0.058, 0.066, 0.30, 7, PANTS)
    box(m, (0, -0.335, -0.025), (0.07, 0.045, 0.105), BOOT)
    return m


def player_torso():
    """The robe: upper body, golden belt, and a skirt flaring down to the ankles."""
    m = Mesh()
    frustum(m, identity, 0.17, 0.145, 0.34, 8, ROBE, bottom_cap=ROBE, top_cap=ROBE)
    frustum(m, identity, 0.178, 0.176, 0.05, 8, GOLD)
    frustum(m, translate(0, -0.29, 0), 0.25, 0.172, 0.29, 10, ROBE, bottom_cap=ROBE_DARK)
    frustum(m, translate(0, -0.29, 0), 0.253, 0.24, 0.04, 10, GOLD)  # hem
    frustum(m, translate(0, 0.33, 0), 0.055, 0.05, 0.07, 6, SKIN)  # neck
    frustum(m, translate(0, 0.29, 0), 0.165, 0.1, 0.06, 8, ROBE_DARK)  # collar
    for a, y in ((2.0, -0.17), (2.6, -0.08), (3.4, -0.2), (3.9, -0.1), (4.5, -0.18), (1.2, -0.2)):
        r = 0.175 - y * 0.269  # on the skirt's surface (radius 0.172 at y = 0, 0.25 at y = -0.29)
        star(m, (r * math.cos(a), y, r * math.sin(a)), 0.03, (math.cos(a), 0.25, math.sin(a)), STAR)
    return m


def player_arm():
    """A wide sleeve with a golden cuff, and the hand."""
    m = Mesh()
    frustum(m, translate(0, -0.23, 0), 0.08, 0.052, 0.23, 8, ROBE, bottom_cap=ROBE_DARK)
    frustum(m, translate(0, -0.235, 0), 0.083, 0.081, 0.03, 8, GOLD)
    blob(m, (0, -0.29, 0), (0.048, 0.055, 0.048), 0.05, 910, solid(SKIN))
    return m


def player_head():
    """Face, hair and the pointed hat with a wide brim and a bent tip."""
    m = Mesh()
    blob(m, (0, 0.2, 0), (0.21, 0.2, 0.2), 0.03, 920, solid(SKIN))
    blob(m, (0, 0.27, 0.06), (0.22, 0.16, 0.2), 0.06, 921, jitter(HAIR, 921, 0.06))
    blob(m, (0, 0.165, -0.2), (0.03, 0.028, 0.03), 0.0, 922, solid(shade(SKIN, 0.92)))  # nose
    for side in (-1, 1):
        blob(m, (side * 0.12, 0.135, -0.155), (0.04, 0.025, 0.02), 0.0, 923, solid(CHEEK))
    frustum(m, translate(0, 0.32, 0.02), 0.36, 0.33, 0.025, 14, ROBE, bottom_cap=ROBE_DARK, top_cap=ROBE)  # brim
    frustum(m, translate(0, 0.34, 0.02), 0.19, 0.11, 0.26, 12, ROBE)
    frustum(m, translate(0, 0.345, 0.02), 0.192, 0.18, 0.05, 12, GOLD)  # band
    # The tip, bent backwards.
    tilt = 0.55
    bend = lambda p: (p[0], 0.6 + p[1] * math.cos(tilt) - p[2] * math.sin(tilt),
                      0.02 + p[1] * math.sin(tilt) + p[2] * math.cos(tilt))  # rotation around X
    frustum(m, bend, 0.11, 0.0, 0.24, 12, ROBE)
    star(m, (0, 0.47, -0.135), 0.045, (0, 0.3, -1), STAR)
    return m


def player_staff():
    """Held in the right hand (origin at the grip), a crystal sits on top (player_crystal)."""
    m = Mesh()
    frustum(m, translate(0, -0.3, 0), 0.022, 0.026, 1.05, 6, STAFF_WOOD, bottom_cap=STAFF_WOOD)
    frustum(m, translate(0, 0.72, 0), 0.03, 0.05, 0.06, 6, GOLD)
    for i in range(3):  # claws holding the crystal
        a = TAU * i / 3
        box(m, (0.045 * math.cos(a), 0.83, 0.045 * math.sin(a)), (0.012, 0.06, 0.012), GOLD)
    return m


def player_crystal():
    """The crystal of the staff (origin at its center), drawn glowing by the game."""
    m = Mesh()
    top, bottom = (0, 0.11, 0), (0, -0.08, 0)
    ring = [(0.055 * math.cos(TAU * i / 6), 0.0, 0.055 * math.sin(TAU * i / 6)) for i in range(6)]
    for i in range(6):
        a, b = ring[i], ring[(i + 1) % 6]
        m.tri(top, b, a, shade(CRYSTAL, 1.0 if i % 2 else 0.85))
        m.tri(bottom, a, b, shade(CRYSTAL, 0.8 if i % 2 else 0.7))
    return m


def companion_body():
    m = Mesh()
    blob(m, (0, 0, 0), (0.17, 0.15, 0.24), 0.04, 930,
         lambda n, face: CREAM if n[1] < -0.35 else shade(FUR, 0.96 + 0.08 * hash01(930, face)))
    return m


def companion_leg():
    m = Mesh()
    frustum(m, translate(0, -0.17, 0), 0.04, 0.05, 0.17, 6, FUR)
    blob(m, (0, -0.18, -0.01), (0.05, 0.035, 0.06), 0.0, 931, solid(PAW))
    return m


def companion_head():
    m = Mesh()
    blob(m, (0, 0.08, 0), (0.16, 0.14, 0.15), 0.03, 932, lambda n, face: shade(FUR, 0.97 + 0.06 * hash01(932, face)))
    blob(m, (0, 0.04, -0.125), (0.075, 0.06, 0.075), 0.03, 933, solid(CREAM))
    blob(m, (0, 0.065, -0.195), (0.026, 0.022, 0.02), 0.0, 934, solid(NOSE))
    for side in (-1, 1):
        blob(m, (side * 0.1, 0.02, -0.08), (0.06, 0.05, 0.06), 0.02, 935, solid(CREAM))
    return m


def companion_ear():
    m = Mesh()
    frustum(m, identity, 0.06, 0.0, 0.15, 5, FUR, bottom_cap=FUR)
    frustum(m, translate(0, 0.1, 0), 0.021, 0.0, 0.05, 5, PAW)
    return m


def companion_tail():
    m = Mesh()
    blob(m, (0, 0.03, 0.09), (0.06, 0.06, 0.11), 0.05, 936, solid(FUR))
    blob(m, (0, 0.1, 0.21), (0.075, 0.075, 0.09), 0.08, 937, solid(CREAM))
    return m


def slime_body():
    m = Mesh()
    light = (0.0, 1.0, 0.0)

    def color(n, face):
        if dot(n, light) > 0.8:
            return shade(SLIME, 1.15)
        if n[1] < -0.2:
            return shade(SLIME, 0.75)
        return shade(SLIME, 0.95 + 0.08 * hash01(940, face))
    blob(m, (0, 0.2, 0), (0.34, 0.27, 0.32), 0.05, 940, color, lambda p: (p[0], max(p[1], -0.2), p[2]))
    m.fit((-0.34, 0, -0.32), (0.34, 0.48, 0.32))
    return m


def mushroom_stem():
    m = Mesh()
    frustum(m, identity, 0.12, 0.09, 0.3, 8, STEM, bottom_cap=STEM)
    return m


def mushroom_cap():
    m = Mesh()

    def color(n, face):
        if n[1] < -0.1:
            return shade(STEM, 0.85)  # underside
        if n[1] > 0.1 and hash01(950, face) < 0.22:
            return WHITE  # dots
        return shade(CAP_RED, 0.95 + 0.08 * hash01(951, face))
    blob(m, (0, 0, 0), (0.32, 0.22, 0.32), 0.06, 950, color, lambda p: (p[0], max(p[1], -0.04), p[2]))
    return m


def mushroom_leg():
    m = Mesh()
    frustum(m, translate(0, -0.11, 0), 0.03, 0.035, 0.11, 6, STEM)
    blob(m, (0, -0.12, -0.015), (0.045, 0.025, 0.06), 0.0, 952, solid(FEET))
    return m


def bird_body():
    m = Mesh()
    blob(m, (0, 0, 0), (0.2, 0.19, 0.21), 0.03, 960,
         lambda n, face: BELLY if (n[2] < -0.35 and n[1] < 0.3) else shade(FEATHER, 0.96 + 0.08 * hash01(960, face)))
    # Beak pointing forward (-Z): a cone built along +Y, turned to -Z.
    frustum(m, lambda p: (p[0], p[2] + 0.02, -p[1] - 0.18), 0.055, 0.0, 0.1, 5, BEAK, bottom_cap=BEAK)
    # Tail feathers, backwards and up.
    frustum(m, lambda p: (p[0], 0.06 + p[1] * 0.5, 0.17 + p[1] * 0.85), 0.07, 0.0, 0.14, 4, shade(FEATHER, 0.85),
            bottom_cap=shade(FEATHER, 0.85))
    return m


def bird_wing():
    m = Mesh()
    blob(m, (0.08, 0, 0.02), (0.09, 0.035, 0.13), 0.04, 961, solid(shade(FEATHER, 0.88)))
    return m


def bird_leg():
    m = Mesh()
    frustum(m, translate(0, -0.11, 0), 0.016, 0.018, 0.11, 5, BEAK)
    box(m, (0, -0.113, -0.03), (0.035, 0.008, 0.045), BEAK)
    return m


# ---------------------------------------------------------------- village

PLASTER = (0.93, 0.88, 0.78, 1)
TIMBER = (0.38, 0.25, 0.15, 1)
STONE = (0.62, 0.60, 0.58, 1)
DARK_STONE = (0.42, 0.40, 0.40, 1)
PLANKS = (0.64, 0.45, 0.28, 1)
DOOR = (0.45, 0.27, 0.14, 1)
GLASS = (0.55, 0.76, 0.90, 1)
ROOF_RED = (0.72, 0.32, 0.22, 1)
ROOF_SLATE = (0.38, 0.44, 0.58, 1)
THATCH = (0.86, 0.72, 0.40, 1)
MUD = (0.74, 0.60, 0.42, 1)
WATER_DARK = (0.12, 0.24, 0.32, 1)
LANTERN = (1.0, 0.86, 0.45, 1)
METAL = (0.25, 0.25, 0.28, 1)


def gable_roof(m, half_x, half_z, eave_y, ridge_y, overhang, color):
    """Two slanted planes, ridge along X, with a darker underside and gable walls."""
    x0, x1 = -half_x - overhang, half_x + overhang
    zf, zb = -half_z - overhang, half_z + overhang
    drop = overhang * (ridge_y - eave_y) / half_z  # the eaves continue the slope
    ye = eave_y - drop
    rows = 4
    for side, z_edge in ((-1, zf), (1, zb)):
        for r in range(rows):  # tile rows, alternately shaded
            t0, t1 = r / rows, (r + 1) / rows
            za, zb_ = z_edge * (1 - t0), z_edge * (1 - t1)
            ya, yb = ye + (ridge_y - ye) * t0, ye + (ridge_y - ye) * t1
            c = shade(color, 1.0 if r % 2 == 0 else 0.9)
            m.quad_facing((x0, ya, za), (x1, ya, za), (x1, yb, zb_), (x0, yb, zb_), c, (0, 1, side))
        under = shade(color, 0.5)
        m.quad_facing((x0, ye, z_edge), (x1, ye, z_edge), (x1, ridge_y, 0), (x0, ridge_y, 0), under, (0, -1, -side))


def gable_walls(m, half_x, half_z, wall_y, ridge_y, color):
    for side in (-1, 1):
        x = side * half_x
        a, b, c = (x, wall_y, -half_z), (x, wall_y, half_z), (x, ridge_y - 0.15, 0)
        if dot(cross(sub(b, a), sub(c, a)), (side, 0, 0)) < 0:
            b, c = c, b
        m.tri(a, b, c, color)


def window(m, center, half_w, half_h, facing):
    """A framed window on a wall facing `facing` (unit axis vector)."""
    fx, fy, fz = facing
    side = (abs(fz), 0, abs(fx))  # along the wall
    def at(u, v, out):
        return (center[0] + side[0] * u + fx * out, center[1] + v, center[2] + side[2] * u + fz * out)
    box(m, at(0, 0, 0.02), (max(side[0] * (half_w + 0.07), abs(fx) * 0.03), half_h + 0.07,
                           max(side[2] * (half_w + 0.07), abs(fz) * 0.03)), TIMBER)
    box(m, at(0, 0, 0.05), (max(side[0] * half_w, abs(fx) * 0.02), half_h, max(side[2] * half_w, abs(fz) * 0.02)), GLASS)
    box(m, at(0, 0, 0.075), (max(side[0] * half_w, abs(fx) * 0.01), 0.025, max(side[2] * half_w, abs(fz) * 0.01)), TIMBER)


def door(m, center, half_w, half_h, facing):
    fx, fy, fz = facing
    hx = max(abs(fz) * half_w, abs(fx) * 0.04)
    hz = max(abs(fx) * half_w, abs(fz) * 0.04)
    box(m, (center[0] + fx * 0.02, center[1] + 0.03, center[2] + fz * 0.02), (hx + abs(fz) * 0.06, half_h + 0.03, hz + abs(fx) * 0.06), TIMBER)
    box(m, (center[0] + fx * 0.05, center[1], center[2] + fz * 0.05), (hx, half_h, hz), DOOR)
    knob = (center[0] + fx * 0.09 + abs(fz) * half_w * 0.6, center[1], center[2] + fz * 0.09 + abs(fx) * half_w * 0.6)
    box(m, knob, (0.035, 0.035, 0.035), LANTERN)


def house_cottage():
    """5 x 4 m, walls 2.6 m, gable roof ridge at 4.3 m along X, door at -Z."""
    m = Mesh()
    hx, hz, wall = 2.5, 2.0, 2.6
    box(m, (0, 0.15, 0), (hx + 0.05, 0.15, hz + 0.05), STONE)
    box(m, (0, 0.3 + (wall - 0.3) / 2, 0), (hx, (wall - 0.3) / 2, hz), PLASTER)
    for sx in (-1, 1):  # timber frame
        for sz in (-1, 1):
            box(m, (sx * (hx - 0.02), wall / 2, sz * (hz - 0.02)), (0.08, wall / 2, 0.08), TIMBER)
    box(m, (0, wall - 0.05, 0), (hx + 0.02, 0.07, hz + 0.02), TIMBER)
    gable_walls(m, hx, hz, wall, 4.3, PLASTER)
    gable_roof(m, hx, hz, wall, 4.3, 0.35, ROOF_RED)
    door(m, (0, 0.95, -hz), 0.45, 0.95, (0, 0, -1))
    for x in (-1.55, 1.55):
        window(m, (x, 1.55, -hz), 0.38, 0.33, (0, 0, -1))
        window(m, (x, 1.55, hz), 0.38, 0.33, (0, 0, 1))
    for sx in (-1, 1):
        window(m, (sx * hx, 1.55, 0), 0.35, 0.33, (sx, 0, 0))
    return m


def house_chimney():
    """4.5 x 4.5 m, two floors (4.2 m), ridge at 5.6 m along X, chimney top at
    local (1.3, 6.5, 0.9): smoke comes out there (see src/render/village.cpp)."""
    m = Mesh()
    hx = hz = 2.25
    box(m, (0, 1.05, 0), (hx, 1.05, hz), STONE)
    box(m, (0, 3.15, 0), (hx + 0.1, 1.05, hz + 0.1), PLANKS)
    for y in (2.6, 3.15, 3.7):  # plank lines
        box(m, (0, y, 0), (hx + 0.12, 0.02, hz + 0.12), shade(PLANKS, 0.8))
    box(m, (0, 2.1, 0), (hx + 0.14, 0.08, hz + 0.14), TIMBER)
    gable_walls(m, hx + 0.1, hz + 0.1, 4.2, 5.6, PLANKS)
    gable_roof(m, hx + 0.1, hz + 0.1, 4.2, 5.6, 0.35, ROOF_SLATE)
    door(m, (0, 0.95, -hz), 0.45, 0.95, (0, 0, -1))
    window(m, (1.3, 1.25, -hz), 0.35, 0.35, (0, 0, -1))
    window(m, (-1.3, 1.25, -hz), 0.35, 0.35, (0, 0, -1))
    for x in (-1.1, 1.1):
        window(m, (x, 3.2, -hz - 0.1), 0.32, 0.32, (0, 0, -1))
        window(m, (x, 3.2, hz + 0.1), 0.32, 0.32, (0, 0, 1))
    for sx in (-1, 1):
        window(m, (sx * hx, 1.25, 0), 0.35, 0.35, (sx, 0, 0))
    # Chimney through the roof.
    box(m, (1.3, 5.0, 0.9), (0.35, 1.5, 0.35), DARK_STONE)
    box(m, (1.3, 6.45, 0.9), (0.42, 0.07, 0.42), shade(DARK_STONE, 0.8))
    return m


def house_hut():
    """Round, radius 2.1 m, walls 2.3 m, thatched cone up to 4.2 m."""
    m = Mesh()
    segments = 14
    for i in range(segments):  # vertical planks, alternately shaded
        a0, a1 = TAU * i / segments, TAU * (i + 1) / segments
        r0, r1 = 2.1, 2.0
        c = shade(MUD, 1.0 if i % 2 else 0.9)
        b0, b1 = (r0 * math.cos(a0), 0, r0 * math.sin(a0)), (r0 * math.cos(a1), 0, r0 * math.sin(a1))
        t0, t1 = (r1 * math.cos(a0), 2.3, r1 * math.sin(a0)), (r1 * math.cos(a1), 2.3, r1 * math.sin(a1))
        m.quad(b0, t0, t1, b1, c)
    frustum(m, identity, 2.12, 2.12, 0.25, segments, DARK_STONE)
    frustum(m, translate(0, 2.15, 0), 2.75, 0.0, 2.07, segments, THATCH, bottom_cap=shade(THATCH, 0.55))
    frustum(m, translate(0, 2.6, 0), 2.33, 2.15, 0.12, segments, shade(THATCH, 0.8))  # a ring of thatch
    door(m, (0, 0.9, -2.03), 0.42, 0.9, (0, 0, -1))
    for a in (0.0, math.pi):
        window(m, (2.05 * math.cos(a), 1.4, 2.05 * math.sin(a)), 0.28, 0.25, (1 if a == 0 else -1, 0, 0))
    return m


def fence():
    """One 2 m segment along X."""
    m = Mesh()
    for x in (-0.95, 0.95):
        box(m, (x, 0.45, 0), (0.06, 0.45, 0.06), PLANKS)
        box(m, (x, 0.93, 0), (0.04, 0.03, 0.04), shade(PLANKS, 0.8))
    for y in (0.32, 0.68):
        box(m, (0, y, 0), (1.0, 0.05, 0.03), shade(PLANKS, 1.08))
    return m


def sign():
    m = Mesh()
    box(m, (0, 0.9, 0), (0.07, 0.9, 0.07), PLANKS)
    box(m, (0, 1.45, -0.09), (0.55, 0.26, 0.035), shade(PLANKS, 1.15))
    for k, (w, y) in enumerate(((0.36, 1.55), (0.42, 1.45), (0.28, 1.35))):  # writing
        box(m, (-0.05 + 0.03 * k, y, -0.13), (w, 0.022, 0.006), TIMBER)
    # An arrow plank pointing right.
    box(m, (0.2, 1.08, -0.09), (0.32, 0.08, 0.03), shade(PLANKS, 1.05))
    m.tri((0.52, 1.18, -0.12), (0.52, 0.98, -0.12), (0.68, 1.08, -0.12), shade(PLANKS, 1.05))
    return m


def well():
    m = Mesh()
    segments = 12
    frustum(m, identity, 0.85, 0.82, 0.9, segments, STONE)
    for i in range(segments):  # rim and inner wall
        a0, a1 = TAU * i / segments, TAU * (i + 1) / segments
        o0, o1 = (0.82 * math.cos(a0), 0.9, 0.82 * math.sin(a0)), (0.82 * math.cos(a1), 0.9, 0.82 * math.sin(a1))
        i0, i1 = (0.62 * math.cos(a0), 0.9, 0.62 * math.sin(a0)), (0.62 * math.cos(a1), 0.9, 0.62 * math.sin(a1))
        m.quad_facing(o0, o1, i1, i0, shade(STONE, 1.1), (0, 1, 0))
        l0, l1 = (i0[0], 0.4, i0[2]), (i1[0], 0.4, i1[2])
        m.quad_facing(i0, i1, l1, l0, DARK_STONE, (-math.cos(a0), 0, -math.sin(a0)))
        m.tri((0, 0.5, 0), (0.62 * math.cos(a1), 0.5, 0.62 * math.sin(a1)), (0.62 * math.cos(a0), 0.5, 0.62 * math.sin(a0)), WATER_DARK)
    for x in (-0.72, 0.72):
        box(m, (x, 1.45, 0), (0.06, 0.55, 0.06), PLANKS)
    box(m, (0, 1.85, 0), (0.78, 0.04, 0.04), PLANKS)
    frustum(m, translate(0, 1.15, 0), 0.12, 0.14, 0.2, 8, PLANKS, bottom_cap=PLANKS)  # bucket
    box(m, (0, 1.6, 0), (0.008, 0.25, 0.008), METAL)  # rope
    gable_roof(m, 0.8, 0.55, 1.95, 2.45, 0.15, ROOF_RED)
    return m


def barrel():
    m = Mesh()
    frustum(m, identity, 0.3, 0.35, 0.45, 10, PLANKS, bottom_cap=PLANKS)
    frustum(m, translate(0, 0.45, 0), 0.35, 0.3, 0.45, 10, PLANKS, top_cap=shade(PLANKS, 1.1))
    for y in (0.12, 0.72):
        frustum(m, translate(0, y, 0), 0.335, 0.34, 0.05, 10, METAL)
    return m


def lamp():
    m = Mesh()
    box(m, (0, 1.1, 0), (0.06, 1.1, 0.06), TIMBER)
    box(m, (0, 0.06, 0), (0.14, 0.06, 0.14), DARK_STONE)
    box(m, (0, 2.29, 0), (0.15, 0.17, 0.15), LANTERN)
    for sx in (-1, 1):
        for sz in (-1, 1):
            box(m, (sx * 0.14, 2.29, sz * 0.14), (0.02, 0.18, 0.02), METAL)
    frustum(m, translate(0, 2.46, 0), 0.22, 0.0, 0.16, 4, METAL, bottom_cap=METAL, phase=math.pi / 4)
    return m



RUIN_STONE = (0.66, 0.63, 0.58, 1)
RUIN_DARK = (0.36, 0.34, 0.36, 1)
IVY = (0.30, 0.50, 0.26, 1)
RUNE = (0.62, 0.92, 1.0, 1)
RUNE_VIOLET = (0.80, 0.58, 1.0, 1)


def stone_ring(m, y0, height, r0, r1, segments, seed, color, phase=0.0):
    """A ring of stone blocks, each with its own shade."""
    for i in range(segments):
        a0, a1 = phase + TAU * i / segments, phase + TAU * (i + 1) / segments
        c = shade(color, 0.88 + 0.2 * hash01(seed, i))
        b0, b1 = (r0 * math.cos(a0), y0, r0 * math.sin(a0)), (r0 * math.cos(a1), y0, r0 * math.sin(a1))
        t0 = (r1 * math.cos(a0), y0 + height, r1 * math.sin(a0))
        t1 = (r1 * math.cos(a1), y0 + height, r1 * math.sin(a1))
        m.quad(b0, t0, t1, b1, c)


def tower():
    """The abandoned magician's tower: round, 2.5 m radius, broken top at
    about 9.2 m with three merlons left up to 10.5 m at radius 2.15 and
    angles 0, 120 and 240 degrees (crows land there, see crow_perches in
    src/core/world.cpp). Door at -Z. Collision: half extents 2.35 x 5.25."""
    m = Mesh()
    segments = 16
    frustum(m, identity, 2.6, 2.5, 0.6, segments, RUIN_DARK)  # plinth
    rows = 15
    for row in range(rows):
        y0 = 0.6 + row * (8.6 / rows)
        r0 = 2.4 - 0.15 * row / rows
        r1 = 2.4 - 0.15 * (row + 1) / rows
        stone_ring(m, y0, 8.6 / rows, r0, r1, segments, 1000 + row, RUIN_STONE, phase=(row % 2) * TAU / 32)
    # The floor of the top, seen from above, and the broken parapet.
    frustum(m, translate(0, 9.15, 0), 2.26, 2.26, 0.05, segments, RUIN_DARK, top_cap=RUIN_DARK)
    for i in range(12):
        a = TAU * i / 12
        intact = i % 4 == 0  # 0, 120 and 240 degrees
        h = 1.3 if intact else 0.15 + 0.6 * hash01(1100, i)
        if not intact and hash01(1101, i) < 0.35:
            continue  # fallen
        c = (2.15 * math.cos(a), 9.2 + h / 2, -2.15 * math.sin(a))
        box(m, c, (0.32, h / 2, 0.32), shade(RUIN_STONE, 0.9 + 0.15 * hash01(1102, i)))
    # Door and windows, dark inside; a band of glowing runes.
    box(m, (0, 1.25, -2.38), (0.62, 1.25, 0.1), RUIN_DARK)
    frustum(m, lambda p: (p[0], 2.5 + p[2], -2.36 - p[1]), 0.62, 0.62, 0.1, 10, RUIN_DARK)
    for i, (a, y) in enumerate(((0.6, 4.2), (2.2, 5.6), (3.6, 4.6), (5.0, 6.8), (1.4, 7.6), (4.2, 8.2))):
        r = 2.33
        n = (math.cos(a), 0, math.sin(a))
        box(m, (r * n[0], y, r * n[2]), (0.14 + 0.1 * abs(n[2]), 0.45, 0.14 + 0.1 * abs(n[0])), RUIN_DARK)
    for i in range(24):
        a = TAU * i / 24
        n = (math.cos(a), 0, math.sin(a))
        if abs(a - 3 * math.pi / 2) < 0.4:
            continue  # not over the door
        y = 3.0 + 0.12 * math.sin(i * 1.7)
        r = 2.33
        color = RUNE if i % 3 else RUNE_VIOLET
        box(m, (r * n[0], y, r * n[2]), (0.07 + 0.05 * abs(n[2]), 0.12 + 0.06 * hash01(1103, i), 0.07 + 0.05 * abs(n[0])), color)
    # Ivy climbing the walls.
    for i in range(9):
        a = TAU * hash01(1104, i)
        y = 0.8 + 6.5 * hash01(1105, i)
        r = 2.36
        blob(m, (r * math.cos(a), y, r * math.sin(a)), (0.55, 0.9, 0.55), 0.25, 1106 + i, jitter(IVY, 1107 + i, 0.12),
             reshape=lambda p, a=a: (p[0] * abs(math.sin(a)) + p[0] * 0.3, p[1], p[2] * abs(math.cos(a)) + p[2] * 0.3))
    return m


def ruin_wall(high):
    """A broken wall 3 m long along X, 0.56 m thick; low: up to 1.0 m (can be
    jumped onto), high: up to 2.2 m. Collision: half 1.5 x 0.5 (or 1.1) x 0.28."""
    m = Mesh()
    top = 2.2 if high else 1.0
    seed = 1200 if high else 1300
    columns = 6
    for k in range(columns):
        x0 = -1.5 + 3.0 * k / columns
        w = 3.0 / columns
        h = top * (0.55 + 0.45 * hash01(seed, k)) if k not in (1, 4) else top
        rows = max(2, int(h / 0.35))
        for r in range(rows):
            y0 = h * r / rows
            hh = h / rows
            off = 0.05 * (hash01(seed + 1, k * 10 + r) - 0.5)
            box(m, (x0 + w / 2 + off, y0 + hh / 2, 0), (w / 2 - 0.01, hh / 2 - 0.01, 0.28 - 0.02 * hash01(seed + 2, r)),
                shade(RUIN_STONE, 0.85 + 0.22 * hash01(seed + 3, k * 10 + r)))
        if hash01(seed + 4, k) < 0.5:  # moss on top
            box(m, (x0 + w / 2, h + 0.02, 0), (w / 2 - 0.03, 0.03, 0.24), IVY)
    return m


def pillar():
    """A broken column: plinth and fluted shaft, unit height (scaled by the
    game to its height), radius 0.34. Collision: half 0.4 x height / 2."""
    m = Mesh()
    box(m, (0, 0.06, 0), (0.42, 0.06, 0.42), RUIN_DARK)
    frustum(m, translate(0, 0.12, 0), 0.34, 0.31, 0.88, 10, RUIN_STONE)
    for i in range(10):  # jagged break at the top
        a0, a1 = TAU * i / 10, TAU * (i + 1) / 10
        y = 1.0 - 0.08 * hash01(1400, i)
        m.tri((0, 0.97, 0), (0.31 * math.cos(a1), y, 0.31 * math.sin(a1)), (0.31 * math.cos(a0), y, 0.31 * math.sin(a0)),
              shade(RUIN_STONE, 1.08))
    return m


def fallen_pillar():
    """A column lying along X, 3 m long, radius 0.34, in two pieces."""
    m = Mesh()
    lie = lambda dx: (lambda p: (p[1] + dx, 0.34 - p[0], p[2]))  # +Y turned to +X (a rotation)
    frustum(m, lie(-1.5), 0.34, 0.33, 1.7, 10, RUIN_STONE, bottom_cap=shade(RUIN_STONE, 0.9), top_cap=shade(RUIN_STONE, 1.1))
    frustum(m, lie(0.28), 0.32, 0.31, 1.2, 10, shade(RUIN_STONE, 0.95), bottom_cap=shade(RUIN_STONE, 1.1),
            top_cap=shade(RUIN_STONE, 1.05))
    box(m, (-1.38, 0.38, 0), (0.12, 0.38, 0.38), RUIN_DARK)
    return m


def rubble():
    """A low pile of stones, radius about 0.75, 0.6 high."""
    m = Mesh()
    for i in range(7):
        a = TAU * hash01(1500, i)
        d = 0.45 * hash01(1501, i)
        r = 0.18 + 0.14 * hash01(1502, i)
        blob(m, (d * math.cos(a), r * 0.7, d * math.sin(a)), (r * 1.2, r * 0.8, r), 0.2, 1503 + i,
             jitter(RUIN_STONE if i % 3 else RUIN_DARK, 1510 + i, 0.1))
    m.fit((-0.75, 0, -0.75), (0.75, 0.6, 0.75))
    return m


def rune_stone():
    """A standing stone 0.6 wide (X), 1.9 high, 0.3 thick, with glowing runes on
    both faces. Collision: half 0.35 x 0.95 x 0.18."""
    m = Mesh()
    blob(m, (0, 0.95, 0), (0.33, 0.97, 0.16), 0.06, 1600, jitter(RUIN_DARK, 1601, 0.1))
    for face in (-1, 1):
        for k in range(5):
            y = 0.5 + 0.26 * k
            w = 0.07 + 0.06 * hash01(1602, k)
            box(m, (0.06 * math.sin(k * 2.1), y, face * 0.155), (w, 0.03 + 0.03 * hash01(1603, k), 0.012),
                RUNE if k % 2 else RUNE_VIOLET)
    m.fit((-0.35, 0, -0.18), (0.35, 1.9, 0.18))
    return m


def sparkle():
    """A small magic spark (radius 1), transparent; tinted and faded by the game."""
    m = Mesh(transparent=True)
    pts = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]
    for a, b, c in ((0, 2, 4), (4, 2, 1), (1, 2, 5), (5, 2, 0), (4, 3, 0), (1, 3, 4), (5, 3, 1), (0, 3, 5)):
        m.tri(pts[a], pts[b], pts[c], (1, 1, 1, 0.85))
    return m


def smoke():
    """One puff (radius 1), drawn transparent: the game fades it out."""
    m = Mesh(transparent=True)
    blob(m, (0, 0, 0), (1, 1, 1), 0.12, 970, lambda n, face: (0.9, 0.9, 0.92, 0.6 + 0.15 * max(n[1], 0)))
    return m


def ripple():
    """A ring on the water (radius 1), transparent."""
    m = Mesh(transparent=True, double_sided=True)
    segments = 24
    up = (0.0, 1.0, 0.0)
    for i in range(segments):
        a0, a1 = TAU * i / segments, TAU * (i + 1) / segments
        o0, o1 = (math.cos(a0), 0, math.sin(a0)), (math.cos(a1), 0, math.sin(a1))
        i0, i1 = (0.8 * math.cos(a0), 0, 0.8 * math.sin(a0)), (0.8 * math.cos(a1), 0, 0.8 * math.sin(a1))
        clear, white = (1, 1, 1, 0.0), (1, 1, 1, 0.7)
        m.tri(i0, i1, o1, None, colors=[white, white, clear], normal=up)
        m.tri(i0, o1, o0, None, colors=[white, clear, clear], normal=up)
    return m


# ---------------------------------------------------------------- village people & animals

def villager_torso():
    """White clothes, tinted by the game (shirt color)."""
    m = Mesh()
    frustum(m, identity, 0.17, 0.145, 0.34, 8, WHITE, bottom_cap=WHITE, top_cap=WHITE)
    frustum(m, identity, 0.176, 0.174, 0.05, 8, (0.35, 0.35, 0.35, 1))  # belt
    return m


def villager_leg():
    m = Mesh()
    frustum(m, translate(0, -0.30, 0), 0.062, 0.072, 0.30, 7, WHITE)
    box(m, (0, -0.335, -0.025), (0.072, 0.045, 0.105), (0.3, 0.3, 0.3, 1))  # shoes, tinted darker
    return m


def villager_arm():
    m = Mesh()
    frustum(m, translate(0, -0.28, 0), 0.045, 0.056, 0.28, 7, WHITE, bottom_cap=WHITE)
    return m


def villager_hand():
    m = Mesh()
    blob(m, (0, -0.02, 0), (0.05, 0.055, 0.05), 0.05, 980, solid(WHITE))
    return m


def villager_head():
    """Skin-colored by the game: everything here is white."""
    m = Mesh()
    frustum(m, translate(0, -0.04, 0), 0.055, 0.05, 0.1, 6, WHITE)  # neck
    blob(m, (0, 0.2, 0), (0.21, 0.2, 0.2), 0.03, 981, solid(WHITE))
    blob(m, (0, 0.165, -0.2), (0.03, 0.028, 0.03), 0.0, 982, solid(shade(WHITE, 0.93)))
    for side in (-1, 1):
        blob(m, (side * 0.12, 0.135, -0.155), (0.04, 0.025, 0.02), 0.0, 983, solid((1.0, 0.8, 0.78, 1)))
    return m


def hair_short():
    m = Mesh()
    blob(m, (0, 0.3, 0.06), (0.22, 0.15, 0.2), 0.06, 984, jitter(WHITE, 984, 0.05))
    return m


def hair_long():
    m = Mesh()
    blob(m, (0, 0.3, 0.05), (0.225, 0.16, 0.21), 0.06, 985, jitter(WHITE, 985, 0.05))
    blob(m, (0, 0.12, 0.12), (0.2, 0.22, 0.12), 0.08, 986, jitter(WHITE, 986, 0.05))
    return m


def hair_bun():
    m = Mesh()
    blob(m, (0, 0.3, 0.06), (0.22, 0.15, 0.2), 0.05, 987, jitter(WHITE, 987, 0.05))
    blob(m, (0, 0.44, 0.1), (0.09, 0.09, 0.09), 0.06, 988, jitter(WHITE, 988, 0.05))
    return m


def hat_cap():
    m = Mesh()
    blob(m, (0, 0.33, 0.02), (0.215, 0.11, 0.21), 0.0, 989, solid(WHITE), lambda p: (p[0], max(p[1], -0.02), p[2]))
    box(m, (0, 0.33, -0.25), (0.15, 0.012, 0.09), shade(WHITE, 0.85))  # visor
    return m


def hat_straw():
    m = Mesh()
    frustum(m, translate(0, 0.33, 0.02), 0.34, 0.32, 0.02, 12, WHITE, bottom_cap=shade(WHITE, 0.8), top_cap=WHITE)
    frustum(m, translate(0, 0.35, 0.02), 0.17, 0.12, 0.12, 10, WHITE, top_cap=WHITE)
    frustum(m, translate(0, 0.355, 0.02), 0.172, 0.165, 0.03, 10, shade(WHITE, 0.6))
    return m


def hat_beanie():
    m = Mesh()
    blob(m, (0, 0.32, 0.03), (0.225, 0.17, 0.215), 0.03, 990, jitter(WHITE, 990, 0.04),
         lambda p: (p[0], max(p[1], -0.05), p[2]))
    blob(m, (0, 0.5, 0.03), (0.06, 0.06, 0.06), 0.1, 991, solid(shade(WHITE, 0.9)))
    return m


CROW = (0.13, 0.13, 0.17, 1)


def crow_body():
    m = Mesh()
    blob(m, (0, 0, 0), (0.13, 0.12, 0.2), 0.04, 992, lambda n, face: shade(CROW, 0.9 + 0.25 * max(n[1], 0)))
    blob(m, (0, 0.1, -0.15), (0.09, 0.085, 0.09), 0.03, 993, solid(CROW))  # head
    frustum(m, lambda p: (p[0], p[2] + 0.09, -p[1] - 0.22), 0.035, 0.0, 0.11, 4, (0.35, 0.35, 0.38, 1),
            bottom_cap=(0.35, 0.35, 0.38, 1))  # beak
    frustum(m, lambda p: (p[0], 0.02 + p[1] * 0.25, 0.16 + p[1]), 0.07, 0.02, 0.16, 4, shade(CROW, 0.8),
            bottom_cap=CROW)  # tail
    return m


def crow_wing():
    m = Mesh()
    blob(m, (0.11, 0, 0.03), (0.12, 0.025, 0.12), 0.05, 994, solid(shade(CROW, 0.85)))
    return m


def crow_leg():
    m = Mesh()
    frustum(m, translate(0, -0.08, 0), 0.01, 0.012, 0.08, 4, (0.3, 0.3, 0.3, 1))
    box(m, (0, -0.082, -0.02), (0.02, 0.005, 0.03), (0.3, 0.3, 0.3, 1))
    return m


FISH = (0.30, 0.62, 0.86, 1)
FISH_BELLY = (0.92, 0.95, 0.98, 1)
FIN = (0.98, 0.62, 0.30, 1)


def fish_body():
    m = Mesh()
    blob(m, (0, 0, 0), (0.24, 0.22, 0.28), 0.03, 995,
         lambda n, face: FISH_BELLY if n[1] < -0.3 else shade(FISH, 0.95 + 0.1 * hash01(995, face)))
    blob(m, (0, -0.04, -0.27), (0.07, 0.05, 0.04), 0.0, 996, solid((0.95, 0.5, 0.55, 1)))  # lips
    frustum(m, translate(0, 0.17, 0.02), 0.07, 0.0, 0.14, 4, FIN, bottom_cap=FIN)  # dorsal fin
    return m


def fish_tail():
    m = Mesh(double_sided=True)
    m.tri((0, 0, 0), (0, 0.16, 0.2), (0, -0.16, 0.2), FIN, normal=(1.0, 0.0, 0.0))
    return m


def fish_fin():
    m = Mesh(double_sided=True)
    m.tri((0, 0, -0.05), (0.13, -0.03, 0.04), (0, 0, 0.06), FIN, normal=(0.0, 1.0, 0.0))
    return m


def all_models():
    models = {
        "cube": cube(),
        "crate": crate(),
        "tree": tree(),
        "pine": pine(),
        "log": log(),
        "stump": stump(),
        "bush": bush(),
        "grass_short": grass(7, 0.18, 0.38, 0.12, 700),
        "grass_tall": grass(11, 0.5, 0.95, 0.2, 701),
        "flower": flower(),
        "shadow": shadow(),
        "eye": eye(),
        "player_leg": player_leg(),
        "player_torso": player_torso(),
        "player_arm": player_arm(),
        "player_head": player_head(),
        "player_staff": player_staff(),
        "player_crystal": player_crystal(),
        "companion_body": companion_body(),
        "companion_leg": companion_leg(),
        "companion_head": companion_head(),
        "companion_ear": companion_ear(),
        "companion_tail": companion_tail(),
        "slime_body": slime_body(),
        "mushroom_stem": mushroom_stem(),
        "mushroom_cap": mushroom_cap(),
        "mushroom_leg": mushroom_leg(),
        "bird_body": bird_body(),
        "bird_wing": bird_wing(),
        "bird_leg": bird_leg(),
        "house_cottage": house_cottage(),
        "house_chimney": house_chimney(),
        "house_hut": house_hut(),
        "fence": fence(),
        "sign": sign(),
        "well": well(),
        "barrel": barrel(),
        "lamp": lamp(),
        "smoke": smoke(),
        "ripple": ripple(),
        "villager_torso": villager_torso(),
        "villager_leg": villager_leg(),
        "villager_arm": villager_arm(),
        "villager_hand": villager_hand(),
        "villager_head": villager_head(),
        "hair_short": hair_short(),
        "hair_long": hair_long(),
        "hair_bun": hair_bun(),
        "hat_cap": hat_cap(),
        "hat_straw": hat_straw(),
        "hat_beanie": hat_beanie(),
        "crow_body": crow_body(),
        "crow_wing": crow_wing(),
        "crow_leg": crow_leg(),
        "fish_body": fish_body(),
        "fish_tail": fish_tail(),
        "fish_fin": fish_fin(),
        "tower": tower(),
        "ruin_wall_low": ruin_wall(False),
        "ruin_wall_high": ruin_wall(True),
        "pillar": pillar(),
        "fallen_pillar": fallen_pillar(),
        "rubble": rubble(),
        "rune_stone": rune_stone(),
        "sparkle": sparkle(),
    }
    for v in range(4):
        models[f"rock{v}"] = rock(v)
    for v in range(3):
        models[f"cloud{v}"] = cloud(v)
    for v in range(2):
        models[f"mountain{v}"] = mountain(v)
    return models


# ---------------------------------------------------------------- glTF writer

def to_glb(name, mesh):
    count = len(mesh.positions)
    floats = lambda rows: b"".join(struct.pack("<%df" % len(r), *r) for r in rows)
    pos_bytes = floats(mesh.positions)
    nrm_bytes = floats(mesh.normals)
    col_bytes = floats([tuple(min(1.0, max(0.0, c)) for c in color) for color in mesh.colors])
    binary = pos_bytes + nrm_bytes + col_bytes

    def view(offset, size):
        return {"buffer": 0, "byteOffset": offset, "byteLength": size, "target": 34962}

    mn = [min(p[k] for p in mesh.positions) for k in range(3)]
    mx = [max(p[k] for p in mesh.positions) for k in range(3)]
    gltf = {
        "asset": {"version": "2.0", "generator": "Monstres tools/make_models.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": name}],
        "meshes": [{"name": name, "primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "COLOR_0": 2}, "material": 0, "mode": 4}]}],
        "materials": [{
            "name": name,
            "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1], "metallicFactor": 0, "roughnessFactor": 1},
            "alphaMode": "BLEND" if mesh.transparent else "OPAQUE",
            "doubleSided": mesh.double_sided,
        }],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": count, "type": "VEC3",
             "min": [round(v, 6) for v in mn], "max": [round(v, 6) for v in mx]},
            {"bufferView": 1, "componentType": 5126, "count": count, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": count, "type": "VEC4"},
        ],
        "bufferViews": [view(0, len(pos_bytes)), view(len(pos_bytes), len(nrm_bytes)),
                        view(len(pos_bytes) + len(nrm_bytes), len(col_bytes))],
        "buffers": [{"byteLength": len(binary)}],
    }
    json_bytes = json.dumps(gltf, separators=(",", ":"), sort_keys=True).encode()
    json_bytes += b" " * (-len(json_bytes) % 4)
    binary += b"\0" * (-len(binary) % 4)
    chunks = (struct.pack("<I4s", len(json_bytes), b"JSON") + json_bytes +
              struct.pack("<I4s", len(binary), b"BIN\0") + binary)
    return struct.pack("<4sII", b"glTF", 2, 12 + len(chunks)) + chunks


def read_glb_floats(data):
    """Returns (json, floats of the BIN chunk) of a .glb written by to_glb."""
    json_len = struct.unpack_from("<I", data, 12)[0]
    doc = json.loads(data[20:20 + json_len])
    bin_start = 20 + json_len + 8
    count = doc["buffers"][0]["byteLength"] // 4
    return doc, struct.unpack_from("<%df" % count, data, bin_start)


def same_model(a, b, tolerance=1e-4):
    doc_a, floats_a = read_glb_floats(a)
    doc_b, floats_b = read_glb_floats(b)
    for doc in (doc_a, doc_b):
        for acc in doc["accessors"]:
            acc.pop("min", None)
            acc.pop("max", None)
    return doc_a == doc_b and len(floats_a) == len(floats_b) and \
        all(abs(x - y) <= tolerance for x, y in zip(floats_a, floats_b))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="only check that assets/models is up to date")
    parser.add_argument("--out", type=pathlib.Path, default=OUT_DIR, help="output directory")
    args = parser.parse_args()

    models = all_models()
    if args.check:
        stale = []
        for name, mesh in models.items():
            path = args.out / f"{name}.glb"
            if not path.exists() or not same_model(path.read_bytes(), to_glb(name, mesh)):
                stale.append(path.name)
        extra = sorted(p.name for p in args.out.glob("*.glb") if p.stem not in models)
        if stale or extra:
            print("Models are not up to date. Run: python3 tools/make_models.py")
            for name in stale:
                print("  outdated or missing:", name)
            for name in extra:
                print("  not generated by the script:", name)
            return 1
        print(f"{len(models)} models up to date.")
        return 0

    args.out.mkdir(parents=True, exist_ok=True)
    for name, mesh in models.items():
        data = to_glb(name, mesh)
        (args.out / f"{name}.glb").write_bytes(data)
        print(f"{name}.glb: {len(mesh.positions) // 3} triangles, {len(data) // 1024} KiB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
