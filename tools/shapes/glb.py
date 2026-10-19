"""Minimal glTF 2.0 binary (.glb) writer for one-mesh, triangle-only models.

Standard library only. A flat-shaded mesh is just a mesh whose vertices are duplicated per face.
"""
import json
import math
import struct

_JSON_CHUNK = 0x4E4F534A
_BIN_CHUNK = 0x004E4942
_FLOAT = 5126
_UINT16 = 5123
_UINT32 = 5125
_ARRAY_BUFFER = 34962
_ELEMENT_ARRAY_BUFFER = 34963


def _clean(value):
    # Rounding hides last-bit differences between platforms' trig; adding 0.0 turns -0.0 into 0.0.
    return round(value, 6) + 0.0


def _pad(data, fill):
    return data + fill * (-len(data) % 4)


def check_winding(positions, normals, indices):
    """Raises if any triangle's counter-clockwise normal disagrees with its vertex normals."""
    for t in range(0, len(indices), 3):
        a, b, c = (positions[i] for i in indices[t:t + 3])
        u = [b[k] - a[k] for k in range(3)]
        w = [c[k] - a[k] for k in range(3)]
        n = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
        if math.sqrt(sum(x * x for x in n)) < 1e-9:
            raise ValueError("degenerate triangle %d" % (t // 3))
        vn = [sum(normals[i][k] for i in indices[t:t + 3]) for k in range(3)]
        if sum(n[k] * vn[k] for k in range(3)) <= 0:
            raise ValueError("triangle %d winds against its normals" % (t // 3))


def write_glb(path, name, positions, normals, uvs, indices):
    if not (len(positions) == len(normals) == len(uvs)):
        raise ValueError("attribute counts differ")
    if len(indices) % 3 or max(indices) >= len(positions):
        raise ValueError("bad indices")
    check_winding(positions, normals, indices)

    pos = [tuple(_clean(c) for c in p) for p in positions]
    pos_bytes = b"".join(struct.pack("<3f", *p) for p in pos)
    nrm_bytes = b"".join(struct.pack("<3f", *(_clean(c) for c in n)) for n in normals)
    uv_bytes = b"".join(struct.pack("<2f", *(_clean(c) for c in uv)) for uv in uvs)
    wide = len(positions) > 65535
    idx_bytes = struct.pack("<%d%s" % (len(indices), "I" if wide else "H"), *indices)

    chunks = [pos_bytes, nrm_bytes, uv_bytes, idx_bytes]
    views = []
    offset = 0
    for i, chunk in enumerate(chunks):
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(chunk),
                      "target": _ELEMENT_ARRAY_BUFFER if i == 3 else _ARRAY_BUFFER})
        offset += len(_pad(chunk, b"\0"))
    binary = b"".join(_pad(c, b"\0") for c in chunks)

    gltf = {
        "asset": {"generator": "ECS3D tools/shapes", "version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": name}],
        "meshes": [{"name": name, "primitives": [
            {"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3}]}],
        "accessors": [
            {"bufferView": 0, "componentType": _FLOAT, "count": len(pos), "type": "VEC3",
             "min": [min(p[k] for p in pos) for k in range(3)],
             "max": [max(p[k] for p in pos) for k in range(3)]},
            {"bufferView": 1, "componentType": _FLOAT, "count": len(normals), "type": "VEC3"},
            {"bufferView": 2, "componentType": _FLOAT, "count": len(uvs), "type": "VEC2"},
            {"bufferView": 3, "componentType": _UINT32 if wide else _UINT16, "count": len(indices),
             "type": "SCALAR"},
        ],
        "bufferViews": views,
        "buffers": [{"byteLength": len(binary)}],
    }
    json_bytes = _pad(json.dumps(gltf, separators=(",", ":")).encode("ascii"), b" ")

    total = 12 + 8 + len(json_bytes) + 8 + len(binary)
    out = struct.pack("<4sII", b"glTF", 2, total)
    out += struct.pack("<II", len(json_bytes), _JSON_CHUNK) + json_bytes
    out += struct.pack("<II", len(binary), _BIN_CHUNK) + binary
    with open(path, "wb") as f:
        f.write(out)


def flat_faces(faces):
    """Flat-shaded mesh of a convex polyhedron.

    faces is a list of polygons, each a list of (x, y, z) corners in cyclic order and either winding.
    Each polygon is wound counter-clockwise seen from outside (away from the shape's centroid),
    fan-triangulated, given its face normal, and given 0-1 UVs from its own planar bounding box.
    Returns (positions, normals, uvs, indices).
    """
    corners = [c for face in faces for c in face]
    centroid = [sum(c[k] for c in corners) / len(corners) for k in range(3)]
    positions, normals, uvs, indices = [], [], [], []
    for face in faces:
        pts = list(face)
        n = _newell(pts)
        center = [sum(p[k] for p in pts) / len(pts) for k in range(3)]
        if sum(n[k] * (center[k] - centroid[k]) for k in range(3)) < 0:
            pts.reverse()
            n = [-x for x in n]
        n = _unit(n)
        u_axis = _unit([pts[1][k] - pts[0][k] for k in range(3)])
        v_axis = _cross(n, u_axis)
        us = [sum(p[k] * u_axis[k] for k in range(3)) for p in pts]
        vs = [sum(p[k] * v_axis[k] for k in range(3)) for p in pts]
        u0, u1, v0, v1 = min(us), max(us), min(vs), max(vs)
        base = len(positions)
        for p, pu, pv in zip(pts, us, vs):
            positions.append(tuple(p))
            normals.append(tuple(n))
            uvs.append(((pu - u0) / (u1 - u0), (pv - v0) / (v1 - v0)))
        for i in range(1, len(pts) - 1):
            indices.extend((base, base + i, base + i + 1))
    return positions, normals, uvs, indices


def _cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def _unit(v):
    length = math.sqrt(sum(x * x for x in v))
    return [x / length for x in v]


def _newell(pts):
    n = [0.0, 0.0, 0.0]
    for i, a in enumerate(pts):
        b = pts[(i + 1) % len(pts)]
        n[0] += (a[1] - b[1]) * (a[2] + b[2])
        n[1] += (a[2] - b[2]) * (a[0] + b[0])
        n[2] += (a[0] - b[0]) * (a[1] + b[1])
    return n
