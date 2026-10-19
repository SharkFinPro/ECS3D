"""Model builders. Each returns (positions, normals, uvs, indices) for glb.write_glb."""
import math

from glb import flat_faces


def _grid(rows, segments, poles):
    """
    Indexed triangles over a grid of vertex-index rows, each segments + 1 wide.

    With poles, the first and last rows are single points repeated per segment, so they contribute
    one triangle per segment instead of a triangle plus a degenerate one.
    """
    indices = []
    for j in range(len(rows) - 1):
        lo, hi = rows[j], rows[j + 1]
        for k in range(segments):
            a, b, c, d = lo[k], lo[k + 1], hi[k + 1], hi[k]
            if poles and j == 0:
                indices.extend((a, c, d))
            elif poles and j == len(rows) - 2:
                indices.extend((a, b, d))
            else:
                indices.extend((a, b, d, b, c, d))
    return indices


def _flip(indices):
    out = list(indices)
    for t in range(0, len(out), 3):
        out[t + 1], out[t + 2] = out[t + 2], out[t + 1]
    return out


def capsule(segments=24, ring_steps=8):
    """Capsule along +Y: radius 1, cylinder y in [-1, 1], hemispheres above and below (height 4)."""
    profile = []
    for j in range(ring_steps + 1):
        angle = -math.pi / 2 + (math.pi / 2) * j / ring_steps
        profile.append((-1.0 + math.sin(angle), math.cos(angle), math.sin(angle), math.cos(angle)))
    for j in range(ring_steps + 1):
        angle = (math.pi / 2) * j / ring_steps
        profile.append((1.0 + math.sin(angle), math.cos(angle), math.sin(angle), math.cos(angle)))

    arc = [0.0]
    for a, b in zip(profile, profile[1:]):
        arc.append(arc[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))

    positions, normals, uvs, rows = [], [], [], []
    for (y, radius, ny, nr), length in zip(profile, arc):
        row = []
        for k in range(segments + 1):
            phi = 2 * math.pi * k / segments
            row.append(len(positions))
            positions.append((radius * math.cos(phi), y, radius * math.sin(phi)))
            normals.append((nr * math.cos(phi), ny, nr * math.sin(phi)))
            uvs.append((k / segments, length / arc[-1]))
        rows.append(row)
    return positions, normals, uvs, _flip(_grid(rows, segments, poles=True))


def torus(major_segments=24, minor_segments=12, major_radius=0.75, minor_radius=0.25):
    """Ring in the XZ plane with outer radius major_radius + minor_radius = 1."""
    positions, normals, uvs, rows = [], [], [], []
    for j in range(minor_segments + 1):
        theta = 2 * math.pi * j / minor_segments
        row = []
        for k in range(major_segments + 1):
            phi = 2 * math.pi * k / major_segments
            ring = major_radius + minor_radius * math.cos(theta)
            row.append(len(positions))
            positions.append((ring * math.cos(phi), minor_radius * math.sin(theta), ring * math.sin(phi)))
            normals.append((math.cos(theta) * math.cos(phi), math.sin(theta), math.cos(theta) * math.sin(phi)))
            uvs.append((k / major_segments, j / minor_segments))
        rows.append(row)
    return positions, normals, uvs, _flip(_grid(rows, major_segments, poles=False))


def plane_tile(uv_extent=2.5):
    """Quad at y = 0 spanning -1..1 in X and Z, facing +Y, UVs 0..uv_extent."""
    positions = [(-1.0, 0.0, -1.0), (-1.0, 0.0, 1.0), (1.0, 0.0, 1.0), (1.0, 0.0, -1.0)]
    normals = [(0.0, 1.0, 0.0)] * 4
    uvs = [(0.0, 0.0), (0.0, uv_extent), (uv_extent, uv_extent), (uv_extent, 0.0)]
    return positions, normals, uvs, [0, 1, 2, 0, 2, 3]


def ramp():
    """Wedge on the -1..1 footprint; the top rises from y = -1 at x = -1 to y = 1 at x = +1."""
    a, b, c, d = (-1, -1, -1), (1, -1, -1), (1, -1, 1), (-1, -1, 1)
    e, f = (1, 1, -1), (1, 1, 1)
    return flat_faces([
        [a, b, c, d],
        [b, e, f, c],
        [a, d, f, e],
        [a, b, e],
        [d, c, f],
    ])
