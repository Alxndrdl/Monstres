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
  cloud0..2              fit [-1, 1] x [0, 1] x [-1, 1] (cloud_low0..2: the same, simpler)
  rain, snow             drops / flakes in a box, tiled around the camera (see rain())
  rain_shaft, bolt       rain seen from afar (radius 1, height 1), lightning (height 1, downwards)
  mountain0..1           fit [-1, 1] x [0, 1] x [-1, 1]
  shadow                 soft black disc of radius 1 on y = 0
  house_*, fence, sign,  real size (see each function), door / front at -Z;
  well, barrel, lamp,    collision boxes in src/core/world.cpp must match
  tower, ruin_*, pillar, (pillar: unit height, scaled by the game)
  fallen_pillar, rubble,
  rune_stone
  sparkle                small transparent spark (radius 1)
  smoke, ripple          transparent puff (radius 1) and water ring (radius 1)
  rune_circle            flat circle of runes (radius 1), transparent: the capture seal

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

# How a triangle is shaded (Mesh.finish):
#   FLAT   its own normal: boxes, planks, rocks, leaves (the low-poly look);
#   ROUND  smooth with its neighbours: what is round in truth (a cylinder's
#          side, a hat, a robe); edges sharper than CREASE stay sharp;
#   LUMP   a blob's faces: smooth on the soft models (the characters), flat
#          on the others (rocks, foliage, clouds);
#   FIXED  the normal given with the triangle (grass).
FLAT, ROUND, LUMP, FIXED = 0, 1, 2, 3
CREASE = math.radians(62)  # between two faces: beyond, the edge is a real one


class Mesh:
    """Triangles without shared vertices; their normals are set by finish()."""

    def __init__(self, transparent=False, double_sided=False):
        self.positions, self.normals, self.colors = [], [], []
        self.kinds = []  # one per triangle
        self.transparent = transparent
        self.double_sided = double_sided

    def tri(self, a, b, c, color, colors=None, normal=None, kind=FLAT):
        n = cross(sub(b, a), sub(c, a))
        if length(n) < 1e-9:
            return  # degenerate, e.g. the tip of a cone
        n = normal or normalize(n)
        for i, p in enumerate((a, b, c)):
            self.positions.append(p)
            self.normals.append(n)
            self.colors.append(colors[i] if colors else color)
        self.kinds.append(FIXED if normal else kind)

    def finish(self, soft=False):
        """Smooth normals where the surface is round (see FLAT, ROUND, LUMP):
        at each corner, the faces around it (same place, same kind of surface)
        that turn by less than CREASE are averaged, weighted by their corner's angle."""
        smooth = [k == ROUND or (soft and k == LUMP) for k in self.kinds]
        faces = []  # per triangle: its unit normal
        around = {}  # place -> [(triangle, corner angle)]
        for t in range(len(self.kinds)):
            a, b, c = self.positions[3 * t:3 * t + 3]
            faces.append(normalize(cross(sub(b, a), sub(c, a))))
            if not smooth[t]:
                continue
            for p, q, r in ((a, b, c), (b, c, a), (c, a, b)):
                u, v = normalize(sub(q, p)), normalize(sub(r, p))
                angle = math.acos(max(-1.0, min(1.0, dot(u, v))))
                around.setdefault(tuple(round(x, 5) for x in p), []).append((t, angle))
        limit = math.cos(CREASE)
        for t in range(len(self.kinds)):
            if not smooth[t]:
                continue
            for i in range(3):
                total = (0.0, 0.0, 0.0)
                for other, angle in around[tuple(round(x, 5) for x in self.positions[3 * t + i])]:
                    if dot(faces[other], faces[t]) >= limit:
                        total = add(total, mul(faces[other], angle))
                self.normals[3 * t + i] = normalize(total) if length(total) > 1e-9 else faces[t]

    def quad(self, a, b, c, d, color, kind=FLAT):
        """Counter-clockwise when seen from the front."""
        self.tri(a, b, c, color, kind=kind)
        self.tri(a, c, d, color, kind=kind)

    def quad_facing(self, a, b, c, d, color, outward, kind=FLAT):
        """A quad whose front faces the `outward` direction, whatever the vertex order."""
        if dot(cross(sub(b, a), sub(c, a)), outward) < 0:
            a, b, c, d = a, d, c, b
        self.quad(a, b, c, d, color, kind)

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


def frustum(m, transform, r0, r1, height, segments, side, bottom_cap=None, top_cap=None, phase=0.0,
            open_ends=False, flat=False):
    """Truncated cone along +Y from y = 0 to `height` (a cone when r1 = 0).
    Its side is round (smooth) from 7 segments on; its ends are closed, in
    the side's color unless a cap's color is given (no hole left to see
    through), but with open_ends (ends that nothing can ever show, on models
    drawn by thousands: a trunk's foot in the ground). `flat`: faceted
    whatever its segments (the trees and what lies among them)."""
    t = transform
    kind = ROUND if segments >= 7 and not flat else FLAT
    if not open_ends:
        bottom_cap = bottom_cap or side
        top_cap = top_cap or side
    for i in range(segments):
        a0 = phase + TAU * i / segments
        a1 = phase + TAU * (i + 1) / segments
        b0, b1 = (r0 * math.cos(a0), 0, r0 * math.sin(a0)), (r0 * math.cos(a1), 0, r0 * math.sin(a1))
        t0, t1 = (r1 * math.cos(a0), height, r1 * math.sin(a0)), (r1 * math.cos(a1), height, r1 * math.sin(a1))
        m.quad(t(b0), t(t0), t(t1), t(b1), side, kind)
        if top_cap and r1 > 0:
            m.tri(t((0, height, 0)), t(t1), t(t0), top_cap)
        if bottom_cap and r0 > 0:
            m.tri(t((0, 0, 0)), t(b0), t(b1), bottom_cap)


def lathe(m, profile, segments, transform=None, phase=0.0):
    """A surface of revolution around +Y, in one piece: `profile` is a list of
    (radius, y, color) going along the surface, the outside on the left when
    going up (so: from the bottom to the top for a vase). Each band between
    two points has the color of its first point. Round (smooth): a sharp turn
    of the profile (more than CREASE) stays a sharp edge. A profile that
    starts and ends on the axis (radius 0) is closed."""
    t = transform or identity
    rings = [[t((r * math.cos(phase + TAU * i / segments), y, r * math.sin(phase + TAU * i / segments)))
              for i in range(segments)] for r, y, _ in profile]
    for k in range(len(profile) - 1):
        color = profile[k][2]
        for i in range(segments):
            j = (i + 1) % segments
            a, b, c, d = rings[k][i], rings[k + 1][i], rings[k + 1][j], rings[k][j]
            m.tri(a, b, c, color, kind=ROUND)  # (degenerate ones, on the axis, are dropped)
            m.tri(a, c, d, color, kind=ROUND)


def arc(r0, y0, r1, y1, bulge, steps, color):
    """Points of a profile from (r0, y0) to (r1, y1), bulging to the right
    of the way (outwards when going up) by `bulge` at its middle; without its last point."""
    dx, dy = r1 - r0, y1 - y0
    d = math.hypot(dx, dy) or 1.0
    nx, ny = dy / d, -dx / d
    out = []
    for k in range(steps):
        s = k / steps
        b = bulge * 4 * s * (1 - s)
        out.append((r0 + dx * s + nx * b, y0 + dy * s + ny * b, color))
    return out


def identity(p):
    return p


def icosphere(detail=1):
    """Unit icosphere subdivided `detail` times (20 faces, 80, 320...): vertices, faces."""
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

    faces = base
    for _ in range(detail):
        verts[:] = [normalize(v) for v in verts]
        split = []
        for a, b, c in faces:
            ab, bc, ca = midpoint(a, b), midpoint(b, c), midpoint(c, a)
            split += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        faces = split
    return [normalize(v) for v in verts], faces


def blob(m, center, radii, noise, seed, face_color, reshape=None, detail=1):
    """A lumpy ball: icosphere with each vertex pushed in or out by up to `noise`
    (detail 2: 320 faces instead of 80, for what is seen up close)."""
    verts, faces = icosphere(detail)
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
        m.tri(a, b, c, face_color(normalize(n), f), kind=LUMP)


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
    frustum(m, identity, 0.2, 0.13, 2.3, 6, BARK, open_ends=True)
    crowns = [((0, 2.75, 0), (1.3, 1.05, 1.3)), ((0.6, 2.45, 0.35), (0.85, 0.75, 0.85)),
              ((-0.55, 2.5, -0.3), (0.9, 0.8, 0.9)), ((0.1, 3.55, -0.1), (0.8, 0.7, 0.8))]
    for k, (center, radii) in enumerate(crowns):
        seed = 11 + k
        blob(m, center, radii, 0.12, seed, jitter(LEAVES, seed))
    return m


def pine():
    m = Mesh()
    frustum(m, identity, 0.18, 0.12, 1.3, 6, BARK, open_ends=True)
    under = shade(NEEDLES, 0.7)
    tiers = [(0.8, 1.35, 1.9, NEEDLES), (1.8, 1.05, 1.7, shade(NEEDLES, 1.07)), (2.8, 0.7, 1.6, shade(NEEDLES, 1.14))]
    for k, (y, radius, height, color) in enumerate(tiers):
        frustum(m, lambda p, y=y: (p[0], p[1] + y, p[2]), radius, 0, height, 8, color,
                bottom_cap=under, phase=0.4 * k, flat=True)
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
    frustum(m, lambda p: (p[1] - 1, 1 - p[0], p[2]), 1, 1, 2, 8, BARK, bottom_cap=WOOD, top_cap=WOOD, flat=True)
    return m


def stump():
    m = Mesh()
    frustum(m, identity, 1, 0.88, 1, 8, BARK, top_cap=WOOD, flat=True)
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


def grass_far(blades, min_height, max_height, spread, seed):
    """The tuft from a little farther: as many blades, a little wider, one triangle each."""
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
        width = 1.25 * (0.035 + 0.02 * hash01(seed, b * 5 + 2))
        tip = add(add(base, (0, height, 0)), mul(out, lean))
        b0, b1 = add(base, mul(side, -width)), add(base, mul(side, width))
        m.tri(b0, b1, tip, None, colors=[GRASS_BASE, GRASS_BASE, GRASS_TIP], normal=up)
    return m


def flower_far():
    """The flower from a little farther: a stem and a flat corolla."""
    m = Mesh(double_sided=True)
    up = (0.0, 1.0, 0.0)
    h, w = 0.32, 0.02
    m.tri((-w, 0, 0), (w, 0, 0), (0, h, 0), STEM)
    for i in range(5):
        a0, a1 = TAU * i / 5, TAU * (i + 1) / 5
        p0 = (0.07 * math.cos(a0), h + 0.02, 0.07 * math.sin(a0))
        p1 = (0.07 * math.cos(a1), h + 0.02, 0.07 * math.sin(a1))
        m.tri((0, h + 0.015, 0), p1, p0, WHITE, normal=up)
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


def cloud(variant, detail=1):
    """detail 0: the same cloud, simpler (from afar)."""
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
             lambda p: (p[0], max(p[1], -0.2), p[2]), detail=detail)
    m.fit((-1, 0, -1), (1, 1, 1))
    return m


# ---------------------------------------------------------------- weather (src/render/weather_view.cpp)

RAIN_TILE, RAIN_HEIGHT = 8.0, 12.0   # must match kRainTile, kRainHeight
SNOW_TILE, SNOW_HEIGHT = 6.0, 8.0    # must match kSnowTile, kSnowHeight


def crossed(m, bottom, top, half_width, colors):
    """A thin streak from `bottom` to `top`: two crossed ribbons (seen from anywhere)."""
    for u in ((half_width, 0, 0), (0, 0, half_width)):
        a, b = sub(bottom, u), add(bottom, u)
        c, d = add(top, u), sub(top, u)
        m.tri(a, b, c, None, colors=[colors[0], colors[0], colors[1]], normal=(0, 1, 0))
        m.tri(a, c, d, None, colors=[colors[0], colors[1], colors[1]], normal=(0, 1, 0))


def rain():
    """Raindrops falling in a box of RAIN_TILE x RAIN_HEIGHT x RAIN_TILE
    meters (origin at the middle of its bottom): the game tiles these boxes
    around the camera and moves them down, wrapping around. Transparent,
    glowing (tinted by the game with the light of the sky)."""
    m = Mesh(transparent=True, double_sided=True)
    side = 9  # drops on a jittered grid: even everywhere, and the boxes tile
    for i in range(side):
        for j in range(side):
            k = i * side + j
            x = -RAIN_TILE / 2 + RAIN_TILE * (i + hash01(1500, k)) / side
            z = -RAIN_TILE / 2 + RAIN_TILE * (j + hash01(1501, k)) / side
            y = RAIN_HEIGHT * hash01(1502, k)
            drop = 0.45 + 0.3 * hash01(1503, k)
            crossed(m, (x, y, z), (x, y + drop, z), 0.007, [(0.85, 0.9, 1.0, 0.55), (0.85, 0.9, 1.0, 0.05)])
    return m


def snow():
    """Snowflakes in a box of SNOW_TILE x SNOW_HEIGHT x SNOW_TILE meters, as rain()."""
    m = Mesh(transparent=True, double_sided=True)
    side = 8
    for i in range(side):
        for j in range(side):
            k = i * side + j
            x = -SNOW_TILE / 2 + SNOW_TILE * (i + hash01(1510, k)) / side
            z = -SNOW_TILE / 2 + SNOW_TILE * (j + hash01(1511, k)) / side
            y = SNOW_HEIGHT * hash01(1512, k)
            r = 0.022 + 0.018 * hash01(1513, k)
            white = (1, 1, 1, 0.95)
            crossed(m, (x, y - r, z), (x, y + r, z), r, [white, white])
    return m


def rain_shaft():
    """Rain (or snow) seen from afar, falling under a cloud: an open
    cylinder of radius 1 from y = 0 (the ground) to 1 (the cloud's base),
    streaky, fading at its top into the cloud. Transparent, double-sided,
    glowing (tinted by the game)."""
    m = Mesh(transparent=True, double_sided=True)
    segments = 14
    rings = [(0.0, 0.28), (0.8, 0.5), (1.0, 0.0)]  # (height, alpha)
    for i in range(segments):
        a0, a1 = TAU * i / segments, TAU * (i + 1) / segments
        k = 0.7 + 0.3 * hash01(1520, i)  # streaks
        for (y0, al0), (y1, al1) in zip(rings, rings[1:]):
            r0, r1 = 1 - 0.12 * y0, 1 - 0.12 * y1  # a little narrower near the cloud
            p = [(r0 * math.cos(a0), y0, r0 * math.sin(a0)), (r0 * math.cos(a1), y0, r0 * math.sin(a1)),
                 (r1 * math.cos(a1), y1, r1 * math.sin(a1)), (r1 * math.cos(a0), y1, r1 * math.sin(a0))]
            c0, c1 = (1, 1, 1, al0 * k), (1, 1, 1, al1 * k)
            m.tri(p[0], p[1], p[2], None, colors=[c0, c0, c1], normal=(0, 1, 0))
            m.tri(p[0], p[2], p[3], None, colors=[c0, c1, c1], normal=(0, 1, 0))
    return m


def bolt():
    """A lightning bolt from the cloud (y = 0) down to the ground (y = -1),
    zigzagging, with two branches: a bright core and a faint glow around.
    Scaled by the game (its height); transparent, glowing."""
    m = Mesh(transparent=True, double_sided=True)

    def channel(start, steps, length, spread, seed, width):
        pts = [start]
        x, y, z = start
        for k in range(steps):
            y -= length / steps
            x += spread * (2 * hash01(seed, 2 * k) - 1)
            z += spread * (2 * hash01(seed, 2 * k + 1) - 1)
            pts.append((x, y, z))
        for a, b in zip(pts, pts[1:]):
            crossed(m, b, a, width * 5, [(0.6, 0.7, 1.0, 0.2), (0.6, 0.7, 1.0, 0.2)])  # glow (seen from afar)
            crossed(m, b, a, width, [(1, 1, 1, 1), (1, 1, 1, 1)])
        return pts
    main = channel((0, 0, 0), 12, 1.0, 0.05, 1530, 0.01)
    channel(main[3], 5, 0.3, 0.05, 1531, 0.006)
    channel(main[6], 4, 0.22, 0.05, 1532, 0.005)
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


def shoe(m, color, half=(0.072, 0.05, 0.108)):
    """A rounded shoe under a leg hanging from its hip (the sole at y = -0.38), its tip forwards."""
    blob(m, (0, -0.335, -0.028), half, 0.0, 905, solid(color), lambda p: (p[0], max(p[1], -0.045), p[2]))


def player_leg():
    """Trousers and boots, under the robe."""
    m = Mesh()
    frustum(m, translate(0, -0.30, 0), 0.058, 0.066, 0.30, 8, PANTS)
    shoe(m, BOOT, (0.07, 0.05, 0.105))
    return m


# The magician's parts are white where the player chooses the color (the game
# tints them: robe, skin, hair, eyes), golden where they stay golden. Dark
# white (ROBE_SHADE) is the inside of the robe and of the hat, darker.
ROBE_SHADE = (0.72, 0.72, 0.72, 1)


def robe_profile(woman):
    """The robe from its hem up to the collar (going up, the outside on the left)."""
    if woman:  # a narrower waist and shoulders, a skirt flaring wider
        profile = [(0.0, -0.29, ROBE_SHADE), (0.262, -0.29, WHITE), (0.258, -0.25, WHITE),
                   (0.162, -0.012, WHITE), (0.166, 0.054, WHITE)]
        profile += arc(0.166, 0.054, 0.146, 0.25, 0.022, 4, WHITE)    # the chest
        profile += arc(0.146, 0.25, 0.078, 0.33, 0.016, 4, WHITE)     # the shoulders
        profile += [(0.078, 0.33, ROBE_SHADE), (0.092, 0.345, ROBE_SHADE), (0.0, 0.35, ROBE_SHADE)]
    else:
        profile = [(0.0, -0.29, ROBE_SHADE), (0.248, -0.29, WHITE), (0.247, -0.25, WHITE),
                   (0.176, -0.012, WHITE), (0.18, 0.054, WHITE)]
        profile += arc(0.18, 0.054, 0.15, 0.26, 0.012, 3, WHITE)
        profile += arc(0.15, 0.26, 0.085, 0.335, 0.018, 4, WHITE)     # the shoulders
        profile += [(0.085, 0.335, ROBE_SHADE), (0.1, 0.35, ROBE_SHADE), (0.0, 0.352, ROBE_SHADE)]
    return profile


def band(m, r0, y0, r1, y1, thick, color, segments=16):
    """A golden band around a robe, from (r0, y0) to (r1, y1), standing out by `thick`."""
    lathe(m, [(r0, y0 - 0.004, color), (r0 + thick, y0, color), (r1 + thick, y1, color), (r1, y1 + 0.004, color)], segments)


def player_robe(woman=False):
    """The robe (white: the game tints it), from the ankles to the collar."""
    m = Mesh()
    lathe(m, robe_profile(woman), 16)
    return m


def player_trims(woman=False):
    """What stays golden on the robe: the hem, the belt, the stars of the skirt."""
    m = Mesh()
    hem = 0.262 if woman else 0.248
    band(m, hem - 0.004, -0.292, hem - 0.008, -0.255, 0.008, GOLD)
    waist = 0.162 if woman else 0.176
    band(m, waist + 0.002, -0.008, waist + 0.004, 0.05, 0.008, GOLD)
    for a, y in ((2.0, -0.17), (2.6, -0.08), (3.4, -0.2), (3.9, -0.1), (4.5, -0.18), (1.2, -0.2)):
        r = profile_radius([(hem, -0.25, None), (waist, -0.012, None)], y) + 0.003
        star(m, (r * math.cos(a), y, r * math.sin(a)), 0.03, (math.cos(a), 0.25, math.sin(a)), STAR)
    return m


def player_neck():
    m = Mesh()
    frustum(m, translate(0, 0.33, 0), 0.056, 0.05, 0.08, 10, WHITE)
    return m


def player_arm():
    """A wide sleeve (white: tinted like the robe), round at the shoulder."""
    m = Mesh()
    profile = [(0.0, -0.215, ROBE_SHADE), (0.078, -0.225, WHITE), (0.084, -0.2, WHITE)]
    profile += arc(0.084, -0.2, 0.054, -0.03, 0.006, 3, WHITE)
    profile += arc(0.054, -0.03, 0.0, 0.018, 0.016, 4, WHITE)
    profile += [(0.0, 0.018, WHITE)]
    lathe(m, profile, 12)
    return m


def player_cuff():
    m = Mesh()
    band(m, 0.078, -0.232, 0.082, -0.205, 0.006, GOLD, 12)
    return m


def profile_radius(profile, y):
    """Radius of a profile going up, at height y."""
    for (r0, y0, _), (r1, y1, _) in zip(profile, profile[1:]):
        if y0 <= y <= y1 and y1 > y0:
            return r0 + (r1 - r0) * (y - y0) / (y1 - y0)
    return 0.0


def player_head(woman=False):
    """The face (white: tinted with the skin), its nose and cheeks; a woman's
    has lashes over the eyes and a touch of color on the lips."""
    m = Mesh()
    blob(m, (0, 0.2, 0), (0.21, 0.2, 0.2), 0.02, 920, solid(WHITE), detail=2)
    blob(m, (0, 0.165, -0.2), (0.03, 0.028, 0.03), 0.0, 922, solid(shade(WHITE, 0.92)))  # nose
    for side in (-1, 1):
        blob(m, (side * 0.12, 0.135, -0.155), (0.04, 0.025, 0.02), 0.0, 923, solid((1.0, 0.8, 0.78, 1)))
    if woman:
        for side in (-1, 1):
            for k in range(3):  # three lashes at the outer corner of each eye
                a = 0.5 + 0.35 * k
                blob(m, (side * (0.085 + 0.02 * math.cos(a)), 0.245 + 0.012 * math.sin(a), -0.18),
                     (0.012, 0.004, 0.006), 0.0, 924, solid(EYE_BLACK), detail=0)
        blob(m, (0, 0.105, -0.188), (0.03, 0.009, 0.012), 0.0, 925, solid((1.0, 0.62, 0.62, 1)), detail=0)
    return m


def under_hat(p):
    """Hair pressed under the hat: above the brim, inside its crown."""
    x, y, z = p
    if y > 0.29:
        dz = z - HAT_AXIS_Z
        r = math.hypot(x, dz)
        room = max(0.05, profile_radius(HAT_CROWN, max(y, 0.345)) - 0.02) if y > 0.345 else 0.19
        if r > room:
            x, dz = x * room / r, dz * room / r
        y = min(y, 0.33 + 0.4 * (y - 0.33)) if y > 0.33 else y  # flattened by it
        z = dz + HAT_AXIS_Z
    return (x, y, z)


def under_hood(p):
    """Hair inside the hood (only what falls below its opening stays out)."""
    c, room = (0.0, 0.215, 0.025), 0.228
    x, y, z = p
    if y > 0.07:
        d = (x - c[0], y - c[1], z - c[2])
        r = length(d)
        if r > room:
            x, y, z = c[0] + d[0] * room / r, c[1] + d[1] * room / r, c[2] + d[2] * room / r
    return (x, y, z)


def player_hair(style, cover=None):
    """Hair (white: tinted): short, long down to the shoulders, or a bun;
    `cover`: shaped to stay inside the hat or the hood ("hat", "hood")."""
    m = Mesh()
    fit = {"hat": under_hat, "hood": under_hood}.get(cover)

    def part(center, radii, noise, seed):
        # blob() reshapes around the ball's center: the fit is in the head's space.
        shape = (lambda q: sub(fit(add(center, q)), center)) if fit else None
        blob(m, center, radii, noise, seed, jitter(WHITE, seed, 0.05), shape, detail=2)
    part((0, 0.27, 0.06), (0.22, 0.16, 0.2), 0.05, 921)
    if style == "long":
        part((0, 0.13, 0.11), (0.22, 0.24, 0.13), 0.06, 926)
    elif style == "bun" and cover != "hood":
        part((0, 0.36, 0.19), (0.085, 0.085, 0.085), 0.05, 927)
    return m


def player_beard():
    """A long beard and a mustache (Isidore's), white: tinted with the hair."""
    m = Mesh()
    blob(m, (0, 0.05, -0.15), (0.14, 0.16, 0.08), 0.06, 928, jitter(WHITE, 928, 0.05),
         lambda p: (p[0] * (0.55 + 0.45 * max(0.0, min(1.0, (p[1] + 0.16) / 0.2))), p[1], p[2]), detail=2)
    for side in (-1, 1):
        blob(m, (side * 0.05, 0.13, -0.195), (0.055, 0.022, 0.03), 0.03, 929, solid(WHITE))
    return m


HAT_AXIS_Z = 0.02


def hat_bend(p):
    """The hat's tip leans backwards (+Z), more and more as it goes up."""
    bend_from, bend_to, tilt = 0.5, 0.86, 1.0
    x, y, z = p
    if y > bend_from:
        h = y - bend_from
        a = tilt * (h / (bend_to - bend_from)) ** 1.3
        y, z = bend_from + h * math.cos(a) - z * math.sin(a), h * math.sin(a) + z * math.cos(a)
    return (x, y, z + HAT_AXIS_Z)


HAT_CROWN = [(0.205, 0.345, WHITE), (0.18, 0.395, WHITE)]
HAT_CROWN += arc(0.18, 0.395, 0.0, 0.86, -0.028, 11, WHITE) + [(0.0, 0.86, WHITE)]


def player_hat():
    """The pointed hat (white: tinted like the robe), one smooth piece from its
    wide drooping brim up its crown to the tip, bent backwards. Closed under
    the brim (inside the hair)."""
    m = Mesh()
    under = [(0.0, 0.31, ROBE_SHADE), (0.16, 0.31, ROBE_SHADE)]
    under += arc(0.16, 0.31, 0.37, 0.298, -0.006, 4, ROBE_SHADE)
    brim = [(0.37, 0.298, WHITE), (0.376, 0.306, WHITE)] + arc(0.376, 0.306, 0.205, 0.345, -0.012, 5, WHITE)
    lathe(m, under + brim + HAT_CROWN, 18, hat_bend)
    return m


def player_hat_band():
    """The hat's golden band and its star."""
    m = Mesh()
    band(m, 0.2, 0.343, 0.182, 0.392, 0.008, GOLD, 18)
    for k in range(len(m.positions)):
        x, y, z = m.positions[k]
        m.positions[k] = (x, y, z + HAT_AXIS_Z)
    y = 0.47
    star(m, (0, y, HAT_AXIS_Z - profile_radius(HAT_CROWN, y) - 0.003), 0.045, (0, 0.38, -1), STAR)
    return m


def player_hood():
    """The robe's hood (white: tinted like the robe), up over the head, open
    on the face, with a little point at the back. Double-sided: its inside
    shows around the face."""
    m = Mesh(double_sided=True)
    verts, faces = icosphere(2)
    c, r = (0, 0.215, 0.025), (0.245, 0.245, 0.245)
    for a, b, d in faces:
        pa, pb, pd = verts[a], verts[b], verts[d]
        mid = normalize(add(add(pa, pb), pd))
        if mid[2] < -0.42 or (mid[2] < -0.1 and mid[1] < -0.45):  # the face's opening, down to the chin
            continue
        if mid[1] < -0.62:  # open underneath, on the neck
            continue
        tri = [add(c, mulv(p, r)) for p in (pa, pb, pd)]
        n = cross(sub(tri[1], tri[0]), sub(tri[2], tri[0]))
        if dot(n, sub(tri[0], c)) < 0:
            tri[1], tri[2] = tri[2], tri[1]
        m.tri(tri[0], tri[1], tri[2], WHITE, kind=ROUND)
    # The point at the back, and the hood's drape down onto the shoulders.
    frustum(m, lambda p: (p[0], 0.33 + p[2] * 0.2 - p[1] * 0.3, 0.22 + p[1] * 0.9 + p[2] * 0.1), 0.07, 0.0, 0.16, 10,
            WHITE, open_ends=True)
    frustum(m, translate(0, -0.04, 0.02), 0.2, 0.19, 0.1, 16, WHITE, open_ends=True)
    return m


def player_wand():
    """A wand, held in the right hand (origin at the grip), a small crystal at its tip."""
    m = Mesh()
    frustum(m, translate(0, -0.07, 0), 0.016, 0.009, 0.36, 8, STAFF_WOOD)
    frustum(m, translate(0, -0.08, 0), 0.02, 0.019, 0.07, 8, GOLD)  # the handle
    frustum(m, translate(0, 0.27, 0), 0.01, 0.018, 0.025, 8, GOLD)  # the tip, holding the crystal
    return m


def player_staff():
    """Held in the right hand (origin at the grip), a crystal sits on top (player_crystal)."""
    m = Mesh()
    frustum(m, translate(0, -0.24, 0), 0.022, 0.026, 0.99, 8, STAFF_WOOD, bottom_cap=STAFF_WOOD)
    frustum(m, translate(0, 0.72, 0), 0.03, 0.05, 0.06, 8, GOLD)
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
    blob(m, (0, 0, 0), (0.17, 0.15, 0.24), 0.03, 930,
         lambda n, face: CREAM if n[1] < -0.35 else shade(FUR, 0.97 + 0.06 * hash01(930, face)), detail=2)
    return m


def companion_leg():
    m = Mesh()
    frustum(m, translate(0, -0.17, 0), 0.04, 0.05, 0.17, 8, FUR)
    blob(m, (0, -0.18, -0.01), (0.05, 0.035, 0.06), 0.0, 931, solid(PAW))
    return m


def companion_head():
    m = Mesh()
    blob(m, (0, 0.08, 0), (0.16, 0.14, 0.15), 0.02, 932, lambda n, face: shade(FUR, 0.98 + 0.04 * hash01(932, face)), detail=2)
    blob(m, (0, 0.04, -0.125), (0.075, 0.06, 0.075), 0.03, 933, solid(CREAM))
    blob(m, (0, 0.065, -0.195), (0.026, 0.022, 0.02), 0.0, 934, solid(NOSE))
    for side in (-1, 1):
        blob(m, (side * 0.1, 0.02, -0.08), (0.06, 0.05, 0.06), 0.02, 935, solid(CREAM))
    return m


def companion_ear():
    m = Mesh()
    frustum(m, identity, 0.06, 0.0, 0.15, 8, FUR, bottom_cap=FUR)
    frustum(m, translate(0, 0.1, 0), 0.021, 0.0, 0.05, 8, PAW)
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
    frustum(m, translate(0, -0.11, 0), 0.03, 0.035, 0.11, 8, STEM)
    blob(m, (0, -0.12, -0.015), (0.045, 0.025, 0.06), 0.0, 952, solid(FEET))
    return m


def bird_body():
    m = Mesh()
    blob(m, (0, 0, 0), (0.2, 0.19, 0.21), 0.03, 960,
         lambda n, face: BELLY if (n[2] < -0.35 and n[1] < 0.3) else shade(FEATHER, 0.96 + 0.08 * hash01(960, face)))
    # Beak pointing forward (-Z): a cone built along +Y, turned to -Z.
    frustum(m, lambda p: (p[0], p[2] + 0.02, -p[1] - 0.18), 0.055, 0.0, 0.1, 8, BEAK, bottom_cap=BEAK)
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
    frustum(m, translate(0, -0.11, 0), 0.016, 0.018, 0.11, 7, BEAK)
    box(m, (0, -0.113, -0.03), (0.035, 0.008, 0.045), BEAK)
    return m


# ---------------------------------------------------------------- Zapou (spark) and Ploufi (axolotl)

SPARK_SKIN = (0.44, 0.40, 0.50, 1)    # Zapou: a slate-grey hedgehog...
SPARK_FACE = (0.93, 0.86, 0.76, 1)
SPARK_DARK = (0.18, 0.15, 0.18, 1)
SPARK_BODY_R = (0.17, 0.14, 0.2)      # ...whose quills crackle (spark_quills, tinted and glowing)
AXOLOTL = (0.98, 0.66, 0.74, 1)
AXOLOTL_BELLY = (1.0, 0.86, 0.88, 1)
GILL = (0.92, 0.36, 0.50, 1)


def spark_body():
    """Zapou: a little hedgehog of lightning, on four paws (rig as Pompon's)."""
    m = Mesh()
    blob(m, (0, 0, 0), SPARK_BODY_R, 0.02, 1600,
         lambda n, face: SPARK_FACE if n[1] < -0.45 else shade(SPARK_SKIN, 0.97 + 0.06 * hash01(1600, face)), detail=2)
    return m


def spark_quills(long=False):
    """Its quills, over its back and sides, pointing out and back (white: the
    game tints them with their glowing color). Origin: the body's center."""
    m = Mesh()
    verts, _ = icosphere(2)
    k = 0
    for v in verts:
        # The upper back and the sides: not the belly, not the face.
        if v[1] < -0.2 or v[2] < -0.55 + 0.4 * max(v[1], 0):
            continue
        k += 1
        if hash01(1601, k) < 0.35:
            continue
        base = mulv(v, SPARK_BODY_R)
        out = normalize(add(normalize(mulv(v, (1 / SPARK_BODY_R[0], 1 / SPARK_BODY_R[1], 1 / SPARK_BODY_R[2]))),
                            (0.0, 0.25, 0.7)))  # swept back
        length = (0.075 + 0.04 * hash01(1602, k)) * (1.45 if long else 1.0)
        base = sub(base, mul(out, 0.01))
        axis = normalize(cross((0.0, 1.0, 0.0), out)) if abs(out[1]) < 0.999 else (1.0, 0.0, 0.0)
        angle = math.acos(max(-1.0, min(1.0, out[1])))

        def turn(p, axis=axis, angle=angle, base=base):
            c, s = math.cos(angle), math.sin(angle)
            q = add(add(mul(p, c), mul(cross(axis, p), s)), mul(axis, dot(axis, p) * (1 - c)))
            return add(base, q)
        frustum(m, turn, 0.016, 0.0, length, 5, WHITE, open_ends=True)
    return m


def spark_head():
    """A small head with a pointed snout, a pale face and a black nose."""
    m = Mesh()
    blob(m, (0, 0.07, 0), (0.125, 0.11, 0.12), 0.02, 1603,
         lambda n, face: SPARK_FACE if n[2] < -0.35 or n[1] < -0.5 else SPARK_SKIN, detail=2)
    # The snout, turned towards -Z.
    frustum(m, lambda p: (p[0], 0.045 - p[2] * 0.3, -0.08 - p[1]), 0.06, 0.025, 0.09, 10, SPARK_FACE)
    blob(m, (0, 0.05, -0.172), (0.022, 0.019, 0.018), 0.0, 1604, solid(SPARK_DARK))
    return m


def spark_ear():
    """A small round ear (origin at its base)."""
    m = Mesh()
    blob(m, (0, 0.03, 0), (0.035, 0.035, 0.015), 0.0, 1605, solid(SPARK_FACE), detail=1)
    return m


def spark_tail():
    """A short tuft (origin at its base, backwards)."""
    m = Mesh()
    blob(m, (0, 0.0, 0.03), (0.03, 0.025, 0.04), 0.0, 1606, solid(SPARK_SKIN))
    return m


def spark_leg():
    m = Mesh()
    frustum(m, translate(0, -0.11, 0), 0.03, 0.038, 0.11, 8, SPARK_SKIN)
    blob(m, (0, -0.115, -0.012), (0.036, 0.022, 0.045), 0.0, 1607, solid(SPARK_DARK))
    return m


def axolotl_body():
    """Ploufi: an axolotl walking low on its four short legs (rig as Pompon's)."""
    m = Mesh()
    blob(m, (0, 0, 0), (0.15, 0.11, 0.26), 0.02, 1610,
         lambda n, face: AXOLOTL_BELLY if n[1] < -0.35 else shade(AXOLOTL, 0.98 + 0.04 * hash01(1610, face)), detail=2)
    return m


def axolotl_head():
    """A wide flat head with a smile."""
    m = Mesh()
    blob(m, (0, 0.04, -0.02), (0.18, 0.105, 0.14), 0.02, 1611, solid(AXOLOTL), detail=2)
    for k in range(7):  # the smile, a thin dark arc under the snout
        a = -0.6 + 1.2 * k / 6
        blob(m, (0.11 * math.sin(a), 0.005 - 0.012 * math.cos(a * 1.5), -0.02 - 0.13 * math.cos(a)),
             (0.012, 0.005, 0.008), 0.0, 1612, solid(shade(GILL, 0.6)), detail=0)
    return m


def axolotl_gills():
    """Three feathery fronds on each side of its head; one side here (origin at
    its base, the fronds fanning out along +X, up and down: the game turns
    them back)."""
    m = Mesh()
    for k, up in enumerate((0.75, 0.15, -0.45)):
        d = normalize((1.0, up, 0.0))
        length = 0.11 - 0.015 * k
        # A tapering stalk, turned from +Y to its direction, with feathery bumps along it.
        axis = normalize(cross((0.0, 1.0, 0.0), d))
        angle = math.acos(max(-1.0, min(1.0, d[1])))

        def turn(p, axis=axis, angle=angle):
            c, s = math.cos(angle), math.sin(angle)
            return add(add(mul(p, c), mul(cross(axis, p), s)), mul(axis, dot(axis, p) * (1 - c)))
        frustum(m, turn, 0.022, 0.006, length, 7, GILL)
        for j in range(3):
            t = length * (0.35 + 0.28 * j)
            blob(m, mul(d, t), (0.017 - 0.003 * j,) * 3, 0.0, 1613 + k * 4 + j, solid(shade(GILL, 1.08)), detail=0)
    return m


def axolotl_tail():
    """A flat paddle, backwards (+Z) from its base."""
    m = Mesh()
    blob(m, (0, 0.02, 0.15), (0.035, 0.09, 0.17), 0.0, 1620, solid(AXOLOTL), lambda p: (p[0] * (1 - 0.5 * max(p[2], 0) / 0.17), p[1], p[2]), detail=2)
    return m


def axolotl_leg():
    m = Mesh()
    frustum(m, translate(0, -0.09, 0), 0.03, 0.04, 0.09, 8, AXOLOTL)
    blob(m, (0, -0.095, -0.012), (0.045, 0.018, 0.045), 0.0, 1621, solid(shade(AXOLOTL, 0.9)))
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
# Alpha 0.985 (opaque meshes): lit from inside at night by the game's shader
# (kLitAtNightAlpha in src/render/include/monstres/render/renderer.hpp).
LIT_AT_NIGHT = 0.985
LIT_LANTERN = LANTERN[:3] + (LIT_AT_NIGHT,)
LIT_GLASS = GLASS[:3] + (LIT_AT_NIGHT,)
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
    # Most windows have someone home at night.
    lit = hash01(int(center[0] * 100 + 7), int(center[1] * 100 + center[2] * 1000)) > 0.3
    box(m, at(0, 0, 0.05), (max(side[0] * half_w, abs(fx) * 0.02), half_h, max(side[2] * half_w, abs(fz) * 0.02)),
        LIT_GLASS if lit else GLASS)
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
    box(m, (0, 2.29, 0), (0.15, 0.17, 0.15), LIT_LANTERN)
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
    """A ring of stone blocks, each with its own shade, on a round wall. With
    a phase, the blocks are half a block further (as bricks are laid): the
    wall itself is the same from a ring to the next, with twice as many
    sides as blocks, so that the rings meet without a gap."""
    shift = 1 if phase else 0
    sides = 2 * segments
    for i in range(sides):
        a0, a1 = TAU * i / sides, TAU * (i + 1) / sides
        c = shade(color, 0.88 + 0.2 * hash01(seed, ((i + shift) // 2) % segments))
        b0, b1 = (r0 * math.cos(a0), y0, r0 * math.sin(a0)), (r0 * math.cos(a1), y0, r0 * math.sin(a1))
        t0 = (r1 * math.cos(a0), y0 + height, r1 * math.sin(a0))
        t1 = (r1 * math.cos(a1), y0 + height, r1 * math.sin(a1))
        m.quad(b0, t0, t1, b1, c, ROUND)


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
    """A broken column: plinth and round shaft, unit height (scaled by the
    game to its height), radius 0.34. Collision: half 0.4 x height / 2."""
    m = Mesh()
    box(m, (0, 0.06, 0), (0.42, 0.06, 0.42), RUIN_DARK)
    sides = 12
    foot = [(0.34 * math.cos(TAU * i / sides), 0.12, 0.34 * math.sin(TAU * i / sides)) for i in range(sides)]
    # The break at the top is jagged: the shaft goes up to it, all around.
    top = [(0.31 * math.cos(TAU * i / sides), 1.0 - 0.08 * hash01(1400, i), 0.31 * math.sin(TAU * i / sides))
           for i in range(sides)]
    for i in range(sides):
        j = (i + 1) % sides
        m.quad(foot[i], top[i], top[j], foot[j], RUIN_STONE, ROUND)
        m.tri((0, 0.95, 0), top[j], top[i], shade(RUIN_STONE, 1.08))
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


CASTLE_STONE = (0.70, 0.67, 0.62, 1)
CASTLE_DARK = (0.30, 0.28, 0.30, 1)
BANNER_RED = (0.72, 0.16, 0.20, 1)
CAVE_ROCK = (0.50, 0.48, 0.47, 1)
CAVE_DARK = (0.08, 0.08, 0.10, 1)
CRYSTAL_VIOLET = (0.78, 0.55, 1.0, 1)
CRYSTAL_CYAN = (0.55, 0.92, 1.0, 1)


def stone_box(m, center, half, seed, color=CASTLE_STONE):
    box(m, center, half, shade(color, 0.88 + 0.2 * hash01(seed, int(center[0] * 13 + center[1] * 7 + center[2] * 3))))


def merlons(m, y, points, half, seed):
    """Crenellations: one block per point (x, z), `half` = half extents."""
    for k, (x, z) in enumerate(points):
        stone_box(m, (x, y + half[1], z), half, seed + k)


def banner(m, top, facing_z, color=BANNER_RED, width=0.55, height=1.3):
    """A hanging banner on a wall facing -Z (facing_z = -1) or +Z, with a golden band."""
    x, y, z = top
    box(m, (x, y - height / 2, z + facing_z * 0.03), (width, height / 2, 0.02), color)
    box(m, (x, y - height * 0.72, z + facing_z * 0.05), (width, 0.06, 0.012), GOLD)
    m.tri((x - width, y - height, z + facing_z * 0.03), (x + width, y - height, z + facing_z * 0.03),
          (x, y - height - 0.35, z + facing_z * 0.03), color, normal=(0, 0, facing_z))


def castle_wall():
    """4 m of curtain wall along X, 1.2 m thick (front, outside, at -Z), the
    walk at 4.4 m and merlons up to 5 m. Collision: half 2 x 2.5 x 0.6."""
    m = Mesh()
    stone_box(m, (0, 0.25, 0), (2.0, 0.25, 0.62), 2000, CASTLE_DARK)
    for row in range(5):
        for k in range(4):
            off = 0.5 if row % 2 else 0.0
            x0 = -2.0 + k * 1.0 + off
            x1 = min(x0 + 1.0, 2.0)
            x0 = max(x0, -2.0)
            if row % 2 and k == 3:
                x1 = 2.0
            stone_box(m, ((x0 + x1) / 2, 0.5 + row * 0.78 + 0.39, 0), ((x1 - x0) / 2 - 0.01, 0.38, 0.6), 2001 + row * 10)
        if row % 2 and True:
            stone_box(m, (-1.75, 0.5 + row * 0.78 + 0.39, 0), (0.24, 0.38, 0.6), 2050 + row)
    merlons(m, 4.4, [(-1.6, -0.45), (-0.55, -0.45), (0.55, -0.45), (1.6, -0.45)], (0.3, 0.3, 0.15), 2100)
    stone_box(m, (0, 4.5, 0.48), (2.0, 0.1, 0.12), 2110)  # low parapet inside
    box(m, (0.0, 2.6, -0.61), (0.07, 0.42, 0.02), CASTLE_DARK)  # an arrow slit
    if hash01(2120, 0) < 2:
        blob(m, (1.3, 0.6, -0.62), (0.6, 0.7, 0.18), 0.25, 2121, jitter(IVY, 2122, 0.12))
    return m


def castle_tower():
    """A round corner tower, radius 2.2 m, 8.4 m high, merlons up to 9.6 m at
    radius 1.95 (crows land on them, see crow_perches in src/core/world.cpp),
    a red flag on a pole. Collision: half 2.2 x 4.5."""
    m = Mesh()
    segments = 14
    frustum(m, identity, 2.3, 2.2, 0.5, segments, CASTLE_DARK)
    rows = 10
    for row in range(rows):
        y0 = 0.5 + row * (7.9 / rows)
        stone_ring(m, y0, 7.9 / rows, 2.2 - 0.04 * row / rows, 2.2 - 0.04 * (row + 1) / rows, segments, 2200 + row,
                   CASTLE_STONE, phase=(row % 2) * TAU / 28)
    frustum(m, translate(0, 8.35, 0), 2.32, 2.32, 0.25, segments, CASTLE_STONE, top_cap=shade(CASTLE_DARK, 1.3))
    for i in range(8):
        a = TAU * i / 8
        c = (1.95 * math.cos(a), 9.0, -1.95 * math.sin(a))
        box(m, c, (0.3, 0.6, 0.3), shade(CASTLE_STONE, 0.9 + 0.15 * hash01(2230, i)))
    for i, (a, y) in enumerate(((0.8, 3.0), (2.6, 5.2), (4.4, 3.8), (5.6, 6.4))):  # arrow slits
        n = (math.cos(a), 0, math.sin(a))
        box(m, (2.21 * n[0], y, 2.21 * n[2]), (0.06 + 0.04 * abs(n[2]), 0.4, 0.06 + 0.04 * abs(n[0])), CASTLE_DARK)
    box(m, (0, 10.2, 0), (0.05, 1.8, 0.05), TIMBER)  # the flag
    for k in range(4):
        x0, x1 = 0.05 + k * 0.28, 0.05 + (k + 1) * 0.28
        wave = 0.08 * math.sin(k * 1.4)
        m.quad((x0, 11.6, wave), (x1, 11.6, wave), (x1, 11.0, wave), (x0, 11.0, wave), BANNER_RED)
        m.quad((x0, 11.0, wave), (x1, 11.0, wave), (x1, 11.6, wave), (x0, 11.6, wave), shade(BANNER_RED, 0.8))
    return m


def castle_gate():
    """The gatehouse: 8 m along X, 3 m deep, 7.5 m high (merlons to 8.1 m),
    front (outside) at -Z. The passage in the middle is 3.2 m wide and 4.2 m
    high, under an arch, its portcullis raised. Solid parts (Colliders, see
    build_castle): x in [-4, -1.6] and [1.6, 4] up to 7.5 m, and over the
    passage from 4.2 to 7.5 m."""
    m = Mesh()
    for side in (-1, 1):
        for row in range(9):
            stone_box(m, (side * 2.8, row * 0.833 + 0.416, 0), (1.2, 0.41, 1.5), 2300 + row * 2 + (side > 0))
    stone_box(m, (0, 5.85, 0), (1.6, 1.65, 1.5), 2320)
    # The arch: stones under the lintel, thinner towards the middle.
    for k in range(9):
        x = -1.6 + (k + 0.5) * 3.2 / 9
        y = 3.4 + 0.8 * math.sqrt(max(0.0, 1 - (x / 1.6) ** 2))
        h = 4.2 - y
        if h > 0.02:
            box(m, (x, y + h / 2 + 0.0, -1.2), (1.6 / 9, h / 2, 0.3), shade(CASTLE_STONE, 0.95 + 0.1 * hash01(2330, k)))
            box(m, (x, y + h / 2 + 0.0, 1.2), (1.6 / 9, h / 2, 0.3), shade(CASTLE_STONE, 0.95 + 0.1 * hash01(2331, k)))
    # The raised portcullis: dark bars just under the lintel.
    for k in range(7):
        box(m, (-1.35 + k * 0.45, 4.1, -0.4), (0.04, 0.1, 0.04), METAL)
    box(m, (0, 4.17, -0.4), (1.5, 0.04, 0.05), METAL)
    merlons(m, 7.5, [(x, -1.25) for x in (-3.6, -2.4, -1.2, 0.0, 1.2, 2.4, 3.6)], (0.3, 0.3, 0.2), 2340)
    merlons(m, 7.5, [(x, 1.25) for x in (-3.6, -1.2, 1.2, 3.6)], (0.3, 0.3, 0.2), 2350)
    banner(m, (-2.8, 6.8, -1.5), -1)
    banner(m, (2.8, 6.8, -1.5), -1)
    for side in (-1, 1):  # torches by the passage
        box(m, (side * 1.9, 3.0, -1.55), (0.06, 0.18, 0.06), TIMBER)
        box(m, (side * 1.9, 3.25, -1.6), (0.1, 0.1, 0.1), LIT_LANTERN)
    return m


def castle_keep():
    """The keep: 9 x 9 m, 11.4 m high, merlons to 12 m, a turret on each
    corner up to 12.6 m (crows land on two of them). Door at -Z (towards the
    gate), lit windows. Collision: half 4.5 x 6 x 4.5."""
    m = Mesh()
    stone_box(m, (0, 0.3, 0), (4.55, 0.3, 4.55), 2400, CASTLE_DARK)
    rows = 12
    for row in range(rows):
        y = 0.6 + row * (10.8 / rows)
        stone_box(m, (0, y + 10.8 / rows / 2, 0), (4.5, 10.8 / rows / 2 - 0.005, 4.5), 2401 + row)
    stone_box(m, (0, 11.25, 0), (4.6, 0.15, 4.6), 2420)
    pts = []
    for k in range(5):
        u = -3.0 + 1.5 * k
        pts += [(u, -4.35), (u, 4.35), (-4.35, u), (4.35, u)]
    merlons(m, 11.4, pts, (0.25, 0.3, 0.25), 2430)
    for sx in (-1, 1):
        for sz in (-1, 1):
            frustum(m, translate(sx * 3.95, 9.0, sz * 3.95), 0.8, 0.8, 3.6, 8, shade(CASTLE_STONE, 1.05),
                    top_cap=CASTLE_DARK)
            frustum(m, translate(sx * 3.95, 12.6, sz * 3.95), 0.95, 0.0, 1.4, 8, ROOF_SLATE, bottom_cap=CASTLE_DARK)
    door(m, (0, 1.6, -4.5), 0.8, 1.0, (0, 0, -1))
    box(m, (0, 2.75, -4.52), (0.9, 0.12, 0.05), CASTLE_DARK)
    for y in (4.4, 7.6):
        for x in (-2.2, 2.2):
            window(m, (x, y, -4.5), 0.35, 0.6, (0, 0, -1))
            window(m, (x, y, 4.5), 0.35, 0.6, (0, 0, 1))
            window(m, (4.5, y, x), 0.35, 0.6, (1, 0, 0))
            window(m, (-4.5, y, x), 0.35, 0.6, (-1, 0, 0))
    banner(m, (0, 9.6, -4.52), -1, width=0.8, height=2.4)
    return m


def cave_mouth():
    """A rock arch against a cliff, front (outside) at -Z, 9.2 m wide. Inside,
    a dark hollow 4.4 m wide (x in [-2.2, 2.2]), 3.8 m high, from z = -1.2 to
    4.6. Solid parts (Colliders, see build_cave): x in [-4.7, -2.1] and
    [2.1, 4.7], the back from z = 4.5, the roof from 3.8 m."""
    m = Mesh()

    def clamp_out(center, lo=None, hi=None):
        def reshape(p):
            q = list(p)
            for k in range(3):
                if lo and lo[k] is not None:
                    q[k] = max(q[k], lo[k] - center[k])
                if hi and hi[k] is not None:
                    q[k] = min(q[k], hi[k] - center[k])
            return tuple(q)
        return reshape

    rock = lambda seed: jitter(CAVE_ROCK, seed, 0.14)
    for side in (-1, 1):
        c = (side * 3.5, 3.2, 1.8)
        lim = (None, None, None)
        blob(m, c, (1.5, 3.6, 3.4), 0.18, 2500 + (side > 0), rock(2510 + (side > 0)),
             reshape=clamp_out(c, lo=(2.2 if side > 0 else None, 0.0, None), hi=(-2.2 if side < 0 else None, None, None)))
        c2 = (side * 3.8, 1.0, -1.3)
        blob(m, c2, (1.0, 1.1, 0.8), 0.25, 2520 + (side > 0), rock(2530 + (side > 0)),
             reshape=clamp_out(c2, lo=(2.25 if side > 0 else None, 0.0, None), hi=(-2.25 if side < 0 else None, None, None)))
    c = (0, 5.4, 1.8)
    blob(m, c, (4.2, 1.5, 3.4), 0.15, 2540, rock(2541), reshape=clamp_out(c, lo=(None, 3.85, None)))
    c = (0, 3.2, 6.0)
    blob(m, c, (3.4, 3.6, 1.6), 0.15, 2550, rock(2551), reshape=clamp_out(c, lo=(None, 0.0, 4.65)))
    # The dark inside: walls, ceiling and floor, darker towards the back.
    def shade_at(z):
        t = min(max((z + 1.2) / 5.8, 0.0), 1.0)
        return mix(shade(CAVE_ROCK, 0.45), CAVE_DARK, t ** 0.6)
    zs = [-1.2, 0.3, 1.8, 3.2, 4.65]
    for z0, z1 in zip(zs, zs[1:]):
        c0, c1 = shade_at(z0), shade_at(z1)
        # Left wall (facing +X), right wall (facing -X), ceiling (facing down), floor (facing up).
        m.tri((-2.19, 0, z0), (-2.19, 3.82, z1), (-2.19, 3.82, z0), None, colors=[c0, c1, c0], normal=(1, 0, 0))
        m.tri((-2.19, 0, z0), (-2.19, 0, z1), (-2.19, 3.82, z1), None, colors=[c0, c1, c1], normal=(1, 0, 0))
        m.tri((2.19, 0, z0), (2.19, 3.82, z0), (2.19, 3.82, z1), None, colors=[c0, c0, c1], normal=(-1, 0, 0))
        m.tri((2.19, 0, z0), (2.19, 3.82, z1), (2.19, 0, z1), None, colors=[c0, c1, c1], normal=(-1, 0, 0))
        m.tri((-2.2, 3.81, z0), (2.2, 3.81, z0), (2.2, 3.81, z1), None, colors=[c0, c0, c1], normal=(0, -1, 0))
        m.tri((-2.2, 3.81, z0), (2.2, 3.81, z1), (-2.2, 3.81, z1), None, colors=[c0, c1, c1], normal=(0, -1, 0))
        f0, f1 = shade(c0, 1.4), shade(c1, 1.4)
        m.tri((-2.2, 0.04, z0), (2.2, 0.04, z1), (2.2, 0.04, z0), None, colors=[f0, f1, f0], normal=(0, 1, 0))
        m.tri((-2.2, 0.04, z0), (-2.2, 0.04, z1), (2.2, 0.04, z1), None, colors=[f0, f1, f1], normal=(0, 1, 0))
    cb = shade_at(4.65)
    m.quad((-2.2, 0, 4.64), (-2.2, 3.82, 4.64), (2.2, 3.82, 4.64), (2.2, 0, 4.64), cb)
    for k in range(6):  # pebbles
        x = -1.6 + 3.2 * hash01(2560, k)
        z = -0.6 + 4.6 * hash01(2561, k)
        blob(m, (x, 0.08, z), (0.18, 0.1, 0.15), 0.3, 2562 + k, jitter(shade(CAVE_ROCK, 0.7), 2570 + k, 0.1))
    return m


def crystal():
    """A cluster of five glowing crystals, about 1 m high, origin at the base
    (made to glow by the game). Collision: half 0.35 x 0.5 (times its scale)."""
    m = Mesh()
    shards = [((0, 0, 0), 1.0, 0.18, 0.0, 0.0), ((0.22, 0, 0.1), 0.65, 0.13, 0.35, 0.3),
              ((-0.2, 0, 0.12), 0.7, 0.14, -0.3, 0.2), ((0.05, 0, -0.22), 0.55, 0.12, 0.1, -0.4),
              ((-0.12, 0, -0.1), 0.45, 0.1, -0.2, -0.3)]
    for k, (base, height, r, tx, tz) in enumerate(shards):
        color = CRYSTAL_VIOLET if k % 2 == 0 else CRYSTAL_CYAN

        def t(p, base=base, tx=tx, tz=tz):
            return (base[0] + p[0] + tx * p[1] * 0.4, base[1] + p[1], base[2] + p[2] + tz * p[1] * 0.4)
        frustum(m, t, r, r * 0.9, height * 0.75, 6, color)
        frustum(m, lambda p, t=t, height=height: t((p[0], p[1] + height * 0.75, p[2])), r * 0.9, 0, height * 0.3, 6,
                shade(color, 1.2))
    return m


def teleporter():
    """A teleporter: a round stone platform (radius 1.5, 0.12 high, walked
    onto: no collision) with a ring of runes, and three standing stones
    around it at radius 1.8. The game lays a glowing circle of runes on top."""
    m = Mesh()
    frustum(m, identity, 1.55, 1.5, 0.12, 18, RUIN_DARK, top_cap=shade(RUIN_STONE, 0.95))
    for i in range(18):
        a = TAU * (i + 0.5) / 18
        box(m, (1.15 * math.cos(a), 0.125, 1.15 * math.sin(a)), (0.08, 0.01, 0.08), RUNE if i % 3 else RUNE_VIOLET)
    for k in range(3):
        a = TAU * (0.25 + k / 3)
        c = (1.85 * math.cos(a), 0.0, 1.85 * math.sin(a))
        blob(m, (c[0], 0.85, c[2]), (0.28, 0.88, 0.22), 0.07, 2700 + k, jitter(RUIN_STONE, 2710 + k, 0.1))
        inward = normalize((-c[0], 0, -c[2]))
        for j in range(3):
            y = 0.6 + 0.35 * j
            box(m, (c[0] + inward[0] * 0.2, y, c[2] + inward[2] * 0.2), (0.06, 0.05, 0.06), RUNE if j % 2 else RUNE_VIOLET)
    return m


def lowpoly_blob(m, center, radii, seed, base):
    """A 20-faced ball: the far version of a crown of leaves."""
    t = (1 + math.sqrt(5)) / 2
    verts = [normalize(v) for v in [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t),
                                    (0, -1, -t), (0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]]
    faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4),
             (11, 10, 2), (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8),
             (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    placed = [add(center, mulv(v, radii)) for v in verts]
    for f, (ia, ib, ic) in enumerate(faces):
        a, b, c = placed[ia], placed[ib], placed[ic]
        n = cross(sub(b, a), sub(c, a))
        if dot(n, sub(mul(add(add(a, b), c), 1 / 3), center)) < 0:
            b, c = c, b
        m.tri(a, b, c, shade(base, 0.9 + 0.2 * hash01(seed, f)))


def tree_far():
    """The tree seen from afar: same size, a few triangles."""
    m = Mesh()
    frustum(m, identity, 0.2, 0.13, 2.3, 4, BARK, open_ends=True)
    lowpoly_blob(m, (0, 2.95, 0), (1.75, 1.45, 1.75), 2600, LEAVES)
    return m


def tree_mid():
    """The tree at a middle distance: its four crowns, each a 20-faced ball."""
    m = Mesh()
    frustum(m, identity, 0.2, 0.13, 2.3, 6, BARK, open_ends=True)
    crowns = [((0, 2.75, 0), (1.3, 1.05, 1.3)), ((0.6, 2.45, 0.35), (0.85, 0.75, 0.85)),
              ((-0.55, 2.5, -0.3), (0.9, 0.8, 0.9)), ((0.1, 3.55, -0.1), (0.8, 0.7, 0.8))]
    for k, (center, radii) in enumerate(crowns):
        lowpoly_blob(m, center, mul(radii, 1.06), 11 + k, LEAVES)
    return m


def bush_far():
    """The bush at a middle distance: two 20-faced balls."""
    m = Mesh()
    lowpoly_blob(m, (0, 0.45, 0), (0.8, 0.52, 0.8), 50, BUSH_LEAVES)
    lowpoly_blob(m, (0.35, 0.42, 0.2), (0.6, 0.45, 0.6), 51, BUSH_LEAVES)
    m.fit((-1, 0, -1), (1, 1, 1))
    return m


def pine_far():
    """The pine seen from afar: one cone."""
    m = Mesh()
    frustum(m, identity, 0.18, 0.12, 1.0, 4, BARK, open_ends=True)
    frustum(m, translate(0, 0.8, 0), 1.35, 0, 3.6, 6, shade(NEEDLES, 1.05), bottom_cap=shade(NEEDLES, 0.7))
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


def rune_circle():
    """The capture seal (radius 1, flat on y = 0): two rings, runes between
    them and a six-pointed star inside, white, tinted and made to glow by the game."""
    m = Mesh(transparent=True, double_sided=True)
    up = (0.0, 1.0, 0.0)
    ink = (1, 1, 1, 0.9)

    def stroke(p, q, width):
        dx, dz = q[0] - p[0], q[1] - p[1]
        n = math.hypot(dx, dz)
        ox, oz = -dz / n * width / 2, dx / n * width / 2
        a, b = (p[0] + ox, 0, p[1] + oz), (p[0] - ox, 0, p[1] - oz)
        c, d = (q[0] - ox, 0, q[1] - oz), (q[0] + ox, 0, q[1] + oz)
        m.tri(a, b, c, ink, normal=up)
        m.tri(a, c, d, ink, normal=up)

    def ring(r0, r1, segments=48):
        for i in range(segments):
            a0, a1 = TAU * i / segments, TAU * (i + 1) / segments
            p = [(r * math.cos(a), 0, r * math.sin(a)) for r, a in ((r0, a0), (r0, a1), (r1, a1), (r1, a0))]
            m.tri(p[0], p[1], p[2], ink, normal=up)
            m.tri(p[0], p[2], p[3], ink, normal=up)

    ring(0.93, 1.0)
    ring(0.66, 0.70)
    # Runes: a few strokes each, in the band between the rings.
    glyphs = 12
    for g in range(glyphs):
        center = TAU * (g + 0.5) / glyphs
        def at(u, v):  # u along the circle (-1..1), v across the band (0..1)
            a = center + u * 0.16
            r = 0.75 + v * 0.13
            return (r * math.cos(a), r * math.sin(a))
        strokes = 2 + int(hash01(2100, g) * 2)
        for k in range(strokes):
            u0, v0 = 2 * hash01(2101, g * 8 + k) - 1, hash01(2102, g * 8 + k)
            u1, v1 = 2 * hash01(2103, g * 8 + k) - 1, hash01(2104, g * 8 + k)
            if abs(u1 - u0) + abs(v1 - v0) < 0.6:
                v1 = 1 - v0
            stroke(at(u0, v0), at(u1, v1), 0.022)
    # A six-pointed star inside.
    for t in range(2):
        pts = [(0.62 * math.cos(TAU * (k / 3 + t / 6)), 0.62 * math.sin(TAU * (k / 3 + t / 6))) for k in range(3)]
        for k in range(3):
            stroke(pts[k], pts[(k + 1) % 3], 0.025)
    return m


# ---------------------------------------------------------------- village people & animals

def villager_torso():
    """White clothes, tinted by the game (shirt color): one piece, from the
    belt to the round shoulders."""
    m = Mesh()
    belt = (0.35, 0.35, 0.35, 1)
    profile = [(0.0, 0.0, WHITE), (0.168, 0.0, belt), (0.178, 0.004, belt), (0.178, 0.046, belt), (0.168, 0.054, WHITE)]
    profile += arc(0.168, 0.054, 0.148, 0.26, 0.012, 3, WHITE)
    profile += arc(0.148, 0.26, 0.06, 0.34, 0.02, 4, WHITE) + [(0.06, 0.34, WHITE), (0.0, 0.342, WHITE)]
    lathe(m, profile, 14)
    return m


def villager_leg():
    m = Mesh()
    frustum(m, translate(0, -0.30, 0), 0.062, 0.072, 0.30, 8, WHITE)
    shoe(m, (0.3, 0.3, 0.3, 1))  # tinted darker
    return m


def villager_arm():
    """A sleeve, round at the shoulder."""
    m = Mesh()
    profile = [(0.0, -0.28, WHITE), (0.045, -0.28, WHITE)]
    profile += arc(0.045, -0.28, 0.056, -0.03, 0.003, 3, WHITE)
    profile += arc(0.056, -0.03, 0.0, 0.016, 0.016, 4, WHITE) + [(0.0, 0.016, WHITE)]
    lathe(m, profile, 10)
    return m


def villager_hand():
    m = Mesh()
    blob(m, (0, -0.02, 0), (0.05, 0.055, 0.05), 0.05, 980, solid(WHITE))
    return m


def villager_head():
    """Skin-colored by the game: everything here is white."""
    m = Mesh()
    frustum(m, translate(0, -0.04, 0), 0.055, 0.05, 0.1, 8, WHITE)  # neck
    blob(m, (0, 0.2, 0), (0.21, 0.2, 0.2), 0.02, 981, solid(WHITE), detail=2)
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
    blob(m, (0, 0.322, -0.2), (0.15, 0.014, 0.13), 0.0, 992, solid(shade(WHITE, 0.85)))  # visor
    return m


def hat_straw():
    """A straw hat in one piece: the brim, the round crown and its ribbon."""
    m = Mesh()
    dark, ribbon = shade(WHITE, 0.8), shade(WHITE, 0.6)
    profile = [(0.0, 0.325, dark), (0.34, 0.325, WHITE), (0.345, 0.335, WHITE)]
    profile += arc(0.345, 0.335, 0.178, 0.352, -0.006, 3, WHITE)
    profile += [(0.178, 0.352, ribbon), (0.168, 0.385, WHITE)]
    profile += arc(0.168, 0.385, 0.0, 0.48, 0.05, 6, WHITE) + [(0.0, 0.48, WHITE)]
    lathe(m, profile, 16, translate(0, 0, 0.02))
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


def eye_iris():
    """An eye whose color is set by the game (tinted): light iris, dark pupil, white highlight."""
    m = Mesh()
    light = normalize((0.45, 0.55, -0.7))
    front = (0.0, 0.0, -1.0)

    def color(n, face):
        if dot(n, light) > 0.86:
            return WHITE
        return EYE_BLACK if dot(n, front) > 0.95 else (0.92, 0.92, 0.92, 1)
    blob(m, (0, 0, 0), (1, 1, 0.6), 0.0, 900, color)
    return m


def horn():
    """A horn, antenna or crest feather, tinted by the game: white, 1 m high,
    base radius 0.25, curving backwards (+Z): its tip is at (0, 1, 0.3)."""
    m = Mesh()
    rings = 5
    seg = 7
    pts = []
    for k in range(rings + 1):
        t = k / rings
        r = 0.25 * (1 - t) + 0.02 * t
        cz = 0.3 * t * t
        pts.append([(r * math.cos(TAU * i / seg), t, cz + r * math.sin(TAU * i / seg)) for i in range(seg)])
    for k in range(rings):
        for i in range(seg):
            j = (i + 1) % seg
            m.quad_facing(pts[k][i], pts[k + 1][i], pts[k + 1][j], pts[k][j],
                          shade(WHITE, 0.9 + 0.1 * k / rings),
                          sub(pts[k][i], (0, pts[k][i][1], 0.3 * (k / rings) ** 2)), ROUND)
    tip = (0, 1.04, 0.32)
    for i in range(seg):
        a, b = pts[rings][i], pts[rings][(i + 1) % seg]
        if dot(cross(sub(b, a), sub(tip, a)), sub(add(a, b), (0, 2.0, 0.6))) < 0:
            a, b = b, a
        m.tri(a, b, tip, WHITE, kind=ROUND)
    base = (0, 0, 0)
    for i in range(seg):  # closed underneath
        m.tri(base, pts[0][i], pts[0][(i + 1) % seg], shade(WHITE, 0.9))
    return m


def bulb():
    """A white ball of radius 1 (glowing lure, antenna tips), tinted by the game."""
    m = Mesh()
    blob(m, (0, 0, 0), (1, 1, 1), 0.0, 990, lambda n, face: shade(WHITE, 0.92 + 0.08 * max(n[1], 0)))
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
        "grass_short_far": grass_far(7, 0.18, 0.38, 0.12, 700),
        "grass_tall_far": grass_far(11, 0.5, 0.95, 0.2, 701),
        "flower_far": flower_far(),
        "shadow": shadow(),
        "eye": eye(),
        "player_leg": player_leg(),
        "player_torso": player_robe(),
        "player_arm": player_arm(),
        "player_head": player_head(),
        "player_staff": player_staff(),
        "player_crystal": player_crystal(),
        "player_torso_woman": player_robe(woman=True),
        "player_trims": player_trims(),
        "player_trims_woman": player_trims(woman=True),
        "player_neck": player_neck(),
        "player_cuff": player_cuff(),
        "player_head_woman": player_head(woman=True),
        "player_hair_short": player_hair("short"),
        "player_hair_long": player_hair("long"),
        "player_hair_bun": player_hair("bun"),
        "player_hair_short_hat": player_hair("short", "hat"),
        "player_hair_long_hat": player_hair("long", "hat"),
        "player_hair_bun_hat": player_hair("bun", "hat"),
        "player_hair_short_hood": player_hair("short", "hood"),
        "player_hair_long_hood": player_hair("long", "hood"),
        "player_beard": player_beard(),
        "player_hat": player_hat(),
        "player_hat_band": player_hat_band(),
        "player_hood": player_hood(),
        "player_wand": player_wand(),
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
        "spark_body": spark_body(),
        "spark_quills": spark_quills(),
        "spark_quills_long": spark_quills(long=True),
        "spark_head": spark_head(),
        "spark_ear": spark_ear(),
        "spark_tail": spark_tail(),
        "spark_leg": spark_leg(),
        "axolotl_body": axolotl_body(),
        "axolotl_head": axolotl_head(),
        "axolotl_gills": axolotl_gills(),
        "axolotl_tail": axolotl_tail(),
        "axolotl_leg": axolotl_leg(),
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
        "eye_iris": eye_iris(),
        "horn": horn(),
        "bulb": bulb(),
        "tower": tower(),
        "ruin_wall_low": ruin_wall(False),
        "ruin_wall_high": ruin_wall(True),
        "pillar": pillar(),
        "fallen_pillar": fallen_pillar(),
        "rubble": rubble(),
        "rune_stone": rune_stone(),
        "castle_wall": castle_wall(),
        "castle_tower": castle_tower(),
        "castle_gate": castle_gate(),
        "castle_keep": castle_keep(),
        "cave_mouth": cave_mouth(),
        "crystal": crystal(),
        "teleporter": teleporter(),
        "tree_far": tree_far(),
        "pine_far": pine_far(),
        "tree_mid": tree_mid(),
        "bush_far": bush_far(),
        "sparkle": sparkle(),
        "rune_circle": rune_circle(),
    }
    for v in range(4):
        models[f"rock{v}"] = rock(v)
    for v in range(3):
        models[f"cloud{v}"] = cloud(v)
        models[f"cloud_low{v}"] = cloud(v, detail=0)
    models["rain"] = rain()
    models["snow"] = snow()
    models["rain_shaft"] = rain_shaft()
    models["bolt"] = bolt()
    for v in range(2):
        models[f"mountain{v}"] = mountain(v)
    # The characters' parts are soft (their lumps are smooth); the rest keeps its facets.
    soft = ("player_", "companion_", "slime_", "mushroom_", "bird_", "villager_", "hair_", "hat_", "crow_", "fish_",
            "eye", "horn", "bulb", "spark_", "axolotl_")
    for name, mesh in models.items():
        mesh.finish(soft=name.startswith(soft))
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
