"""The convex shape library: vertex formulas and flat-shaded models.

Every formula is mirrored in source/apps/server/DefaultShapes.cpp, which hulls the same points. Math is
done in double so both sides agree to float precision.
"""
import itertools
import math

from glb import flat_faces

PHI = (1.0 + math.sqrt(5.0)) / 2.0

BOULDER_SEED = 12345
BOULDER_POINTS = 24
BOULDER_SQUASH = 0.7
BOULDER_MIN_RADIUS = 0.75
BOULDER_JITTER = 0.2

_PLANE_EPSILON = 1e-7
_MASK32 = 0xFFFFFFFF


def d4_points():
    return [(1.0, 1.0, 1.0), (1.0, -1.0, -1.0), (-1.0, 1.0, -1.0), (-1.0, -1.0, 1.0)]


def d8_points():
    return [(1.0, 0.0, 0.0), (-1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, -1.0, 0.0),
            (0.0, 0.0, 1.0), (0.0, 0.0, -1.0)]


def d12_points():
    inv = 1.0 / PHI
    points = [(x, y, z) for x in (-1.0, 1.0) for y in (-1.0, 1.0) for z in (-1.0, 1.0)]
    for a in (-1.0, 1.0):
        for b in (-1.0, 1.0):
            points.append((0.0, a * inv, b * PHI))
            points.append((a * inv, b * PHI, 0.0))
            points.append((b * PHI, 0.0, a * inv))
    return [(x / PHI, y / PHI, z / PHI) for x, y, z in points]


def d20_points():
    points = []
    for a in (-1.0, 1.0):
        for b in (-1.0, 1.0):
            points.append((0.0, a, b * PHI))
            points.append((a, b * PHI, 0.0))
            points.append((b * PHI, 0.0, a))
    return [(x / PHI, y / PHI, z / PHI) for x, y, z in points]


def prism_points(radius, half_height, sides):
    points = []
    for y in (-half_height, half_height):
        for k in range(sides):
            angle = 2.0 * math.pi * k / sides
            points.append((radius * math.cos(angle), y, radius * math.sin(angle)))
    return points


def hex_prism_points():
    return prism_points(0.6, 1.0, 6)


def drum_points():
    return prism_points(1.0, 0.45, 16)


def gem_points():
    """Brilliant cut: a point below, an 8-sided girdle, an 8-sided table above."""
    points = [(0.0, -1.0, 0.0)]
    for y, radius in ((0.1, 1.0), (0.55, 0.55)):
        for k in range(8):
            angle = 2.0 * math.pi * k / 8
            points.append((radius * math.cos(angle), y, radius * math.sin(angle)))
    return points


def boulder_points():
    state = BOULDER_SEED

    def uniform():
        nonlocal state
        state ^= (state << 13) & _MASK32
        state ^= state >> 17
        state ^= (state << 5) & _MASK32
        return state / 4294967296.0

    golden = math.pi * (3.0 - math.sqrt(5.0))
    points = []
    for i in range(BOULDER_POINTS):
        y = 1.0 - 2.0 * (i + 0.5) / BOULDER_POINTS
        ring = math.sqrt(1.0 - y * y)
        angle = i * golden
        jx = (uniform() - 0.5) * 2.0 * BOULDER_JITTER
        jy = (uniform() - 0.5) * 2.0 * BOULDER_JITTER
        jz = (uniform() - 0.5) * 2.0 * BOULDER_JITTER
        x = ring * math.cos(angle) + jx
        y = y + jy
        z = ring * math.sin(angle) + jz
        length = math.sqrt(x * x + y * y + z * z)
        radius = BOULDER_MIN_RADIUS + (1.0 - BOULDER_MIN_RADIUS) * uniform()
        points.append((x / length * radius, y / length * radius * BOULDER_SQUASH, z / length * radius))
    return points


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _unit(v):
    length = math.sqrt(_dot(v, v))
    return (v[0] / length, v[1] / length, v[2] / length)


def convex_faces(points):
    """Faces of the convex hull of a small point set as polygons of its points (coplanar triangles merged).

    Brute force over point triples; points that are not hull vertices are dropped. Raises if a point sits
    close to a face plane without being on it, since the C++ hull could then disagree about it.
    """
    planes = []
    for i, j, k in itertools.combinations(range(len(points)), 3):
        n = _cross(_sub(points[j], points[i]), _sub(points[k], points[i]))
        if math.sqrt(_dot(n, n)) < 1e-9:
            continue
        n = _unit(n)
        d = _dot(n, points[i])
        distances = [_dot(n, p) - d for p in points]
        if max(distances) > _PLANE_EPSILON:
            if min(distances) < -_PLANE_EPSILON:
                continue
            n, d, distances = (-n[0], -n[1], -n[2]), -d, [-s for s in distances]
        for pn, pd, _ in planes:
            if _dot(n, pn) > 1.0 - 1e-9 and abs(d - pd) < 1e-6:
                break
        else:
            planes.append((n, d, distances))

    faces = []
    for n, d, distances in planes:
        for s in distances:
            if _PLANE_EPSILON < abs(s) < 1e-3:
                raise ValueError("a point is nearly on a face plane")
        faces.append((n, [idx for idx, s in enumerate(distances) if abs(s) <= _PLANE_EPSILON]))

    uses = {}
    for _, ids in faces:
        for idx in ids:
            uses[idx] = uses.get(idx, 0) + 1
    vertices = {idx for idx, count in uses.items() if count >= 3}

    polygons = []
    for n, ids in faces:
        ids = [idx for idx in ids if idx in vertices]
        center = tuple(sum(points[idx][k] for idx in ids) / len(ids) for k in range(3))
        u = _unit(_sub(points[ids[0]], center))
        v = _cross(n, u)
        ids.sort(key=lambda idx: math.atan2(_dot(_sub(points[idx], center), v),
                                            _dot(_sub(points[idx], center), u)))
        polygons.append([points[idx] for idx in ids])

    edges = sum(len(p) for p in polygons) // 2
    if len(vertices) - edges + len(polygons) != 2:
        raise ValueError("hull is not a closed polyhedron")
    return polygons


def _model(points):
    return flat_faces(convex_faces(points))


def d4():
    return _model(d4_points())


def d8():
    return _model(d8_points())


def d12():
    return _model(d12_points())


def d20():
    return _model(d20_points())


def hex_prism():
    return _model(hex_prism_points())


def drum():
    return _model(drum_points())


def gem():
    return _model(gem_points())


def boulder():
    return _model(boulder_points())
