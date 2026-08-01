// New file - no upstream counterpart. Upstream loads models through Assimp, which handles .obj and
// .glb alike; the WebGPU port carries an OBJ reader only and substitutes procedural meshes for the
// two .glb test assets (see Model.ts's DEVIATION).
//
// Every ECS3D asset is .glb (cube_1x1x1, sphere, sphere_2, sphere_3, square), and the AssetRegistry
// can name a new one at runtime, so the client needs a real loader rather than offline conversion.
//
// Scope: the subset glTF 2.0 needs to produce one merged mesh in Model's vertex layout -
// position(3) normal(3) texCoord(2). Materials, animation, skinning, cameras and morph targets are
// ignored: ECS3D binds its own diffuse/specular textures per ModelRenderer, so a model contributes
// geometry only. Sparse accessors and non-triangle primitive modes are unsupported.
const GLB_MAGIC = 0x46546c67; // "glTF"
const CHUNK_JSON = 0x4e4f534a; // "JSON"
const CHUNK_BIN = 0x004e4942; // "BIN\0"
const VERTEX_FLOATS = 8; // position(3) normal(3) texCoord(2) - see vertexInputs/Vertex.ts
const COMPONENT_SIZE = {
    5120: 1, // BYTE
    5121: 1, // UNSIGNED_BYTE
    5122: 2, // SHORT
    5123: 2, // UNSIGNED_SHORT
    5125: 4, // UNSIGNED_INT
    5126: 4, // FLOAT
};
const TYPE_COMPONENTS = {
    SCALAR: 1,
    VEC2: 2,
    VEC3: 3,
    VEC4: 4,
    MAT4: 16,
};
export async function loadGlb(url) {
    const response = await fetch(url);
    if (!response.ok) {
        throw new Error(`Failed to fetch model: ${url} (${response.status})`);
    }
    const { json, bin } = parseContainer(await response.arrayBuffer(), url);
    return buildMesh(json, bin, url);
}
// A .glb is a 12-byte header followed by length-prefixed chunks: JSON first, then an optional BIN.
function parseContainer(data, url) {
    const view = new DataView(data);
    if (data.byteLength < 12 || view.getUint32(0, true) !== GLB_MAGIC) {
        throw new Error(`Not a GLB file: ${url}`);
    }
    const version = view.getUint32(4, true);
    if (version !== 2) {
        throw new Error(`Unsupported GLB version ${version}: ${url}`);
    }
    let json = null;
    let bin = new Uint8Array(0);
    let offset = 12;
    while (offset + 8 <= data.byteLength) {
        const chunkLength = view.getUint32(offset, true);
        const chunkType = view.getUint32(offset + 4, true);
        const chunkStart = offset + 8;
        if (chunkStart + chunkLength > data.byteLength) {
            throw new Error(`Truncated GLB chunk: ${url}`);
        }
        if (chunkType === CHUNK_JSON) {
            json = JSON.parse(new TextDecoder().decode(new Uint8Array(data, chunkStart, chunkLength)));
        }
        else if (chunkType === CHUNK_BIN) {
            bin = new Uint8Array(data, chunkStart, chunkLength);
        }
        // Chunks are padded to a 4-byte boundary.
        offset = chunkStart + chunkLength + ((4 - (chunkLength % 4)) % 4);
    }
    if (!json) {
        throw new Error(`GLB has no JSON chunk: ${url}`);
    }
    return { json, bin };
}
// Walks the default scene, applying each node's world transform to its mesh primitives and merging
// everything into one vertex/index pair - Model holds a single buffer pair, and ECS3D's models are
// single-material props.
function buildMesh(json, bin, url) {
    const vertices = [];
    const indices = [];
    const sceneIndex = json.scene ?? 0;
    const roots = json.scenes?.[sceneIndex]?.nodes ?? json.nodes?.map((_, i) => i) ?? [];
    const visited = new Set();
    const walk = (nodeIndex, parentMatrix) => {
        // A malformed file could cycle; a node is only ever reached once in a valid scene graph.
        if (visited.has(nodeIndex)) {
            return;
        }
        visited.add(nodeIndex);
        const node = json.nodes?.[nodeIndex];
        if (!node) {
            return;
        }
        const worldMatrix = multiply(parentMatrix, localMatrix(node));
        if (node.mesh !== undefined) {
            const mesh = json.meshes?.[node.mesh];
            for (const primitive of mesh?.primitives ?? []) {
                appendPrimitive(json, bin, primitive, worldMatrix, vertices, indices, url);
            }
        }
        for (const child of node.children ?? []) {
            walk(child, worldMatrix);
        }
    };
    for (const root of roots) {
        walk(root, identity());
    }
    if (indices.length === 0) {
        throw new Error(`GLB contains no triangle geometry: ${url}`);
    }
    return { vertices: new Float32Array(vertices), indices: new Uint32Array(indices) };
}
function appendPrimitive(json, bin, primitive, worldMatrix, vertices, indices, url) {
    // 4 == TRIANGLES. Strips/fans/lines/points are not emitted by the ECS3D assets.
    if ((primitive.mode ?? 4) !== 4) {
        return;
    }
    const positionAccessor = primitive.attributes.POSITION;
    if (positionAccessor === undefined) {
        return;
    }
    const positions = readAccessor(json, bin, positionAccessor, url);
    const normals = primitive.attributes.NORMAL !== undefined
        ? readAccessor(json, bin, primitive.attributes.NORMAL, url)
        : null;
    const texCoords = primitive.attributes.TEXCOORD_0 !== undefined
        ? readAccessor(json, bin, primitive.attributes.TEXCOORD_0, url)
        : null;
    const vertexCount = positions.length / 3;
    const baseVertex = vertices.length / VERTEX_FLOATS;
    // Normals transform by the inverse-transpose so non-uniform node scale doesn't skew them.
    const normalMatrix = transpose(invert(worldMatrix));
    for (let i = 0; i < vertexCount; ++i) {
        const [px, py, pz] = transformPoint(worldMatrix, positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]);
        vertices.push(px, py, pz);
        if (normals) {
            const [nx, ny, nz] = transformDirection(normalMatrix, normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]);
            const length = Math.hypot(nx, ny, nz) || 1;
            vertices.push(nx / length, ny / length, nz / length);
        }
        else {
            vertices.push(0, 0, 0); // filled in by computeNormals below
        }
        // glTF's UV origin is top-left, the same as WebGPU's, so v is NOT flipped here - unlike the OBJ
        // path in Model.ts, whose origin is bottom-left.
        vertices.push(texCoords ? texCoords[i * 2] : 0, texCoords ? texCoords[i * 2 + 1] : 0);
    }
    if (primitive.indices !== undefined) {
        const primitiveIndices = readAccessor(json, bin, primitive.indices, url);
        for (const index of primitiveIndices) {
            indices.push(baseVertex + index);
        }
    }
    else {
        for (let i = 0; i < vertexCount; ++i) {
            indices.push(baseVertex + i);
        }
    }
    if (!normals) {
        computeNormals(vertices, indices, baseVertex);
    }
}
function readAccessor(json, bin, index, url) {
    const accessor = json.accessors?.[index];
    if (!accessor) {
        throw new Error(`GLB accessor ${index} is missing: ${url}`);
    }
    const components = TYPE_COMPONENTS[accessor.type];
    const componentSize = COMPONENT_SIZE[accessor.componentType];
    if (!components || !componentSize) {
        throw new Error(`Unsupported accessor ${accessor.type}/${accessor.componentType}: ${url}`);
    }
    const out = new Float64Array(accessor.count * components);
    // A bufferView-less accessor reads as zeros per spec.
    if (accessor.bufferView === undefined) {
        return out;
    }
    const bufferView = json.bufferViews?.[accessor.bufferView];
    if (!bufferView) {
        throw new Error(`GLB bufferView ${accessor.bufferView} is missing: ${url}`);
    }
    // Only the embedded BIN chunk is supported - a GLB naming an external .bin would need a second
    // fetch, and nothing in the ECS3D asset set does that.
    if (bufferView.buffer !== 0) {
        throw new Error(`GLB references an external buffer, which is unsupported: ${url}`);
    }
    const base = (bufferView.byteOffset ?? 0) + (accessor.byteOffset ?? 0);
    const elementSize = componentSize * components;
    const stride = bufferView.byteStride || elementSize;
    const view = new DataView(bin.buffer, bin.byteOffset, bin.byteLength);
    for (let i = 0; i < accessor.count; ++i) {
        for (let c = 0; c < components; ++c) {
            const offset = base + i * stride + c * componentSize;
            out[i * components + c] = readComponent(view, offset, accessor.componentType);
        }
    }
    return out;
}
function readComponent(view, offset, componentType) {
    switch (componentType) {
        case 5120:
            return view.getInt8(offset);
        case 5121:
            return view.getUint8(offset);
        case 5122:
            return view.getInt16(offset, true);
        case 5123:
            return view.getUint16(offset, true);
        case 5125:
            return view.getUint32(offset, true);
        default:
            return view.getFloat32(offset, true);
    }
}
// Area-weighted vertex normals for a primitive that shipped without NORMAL, matching the fallback
// the OBJ path in Model.ts uses.
function computeNormals(vertices, indices, baseVertex) {
    for (let i = 0; i < indices.length; i += 3) {
        const a = indices[i] * VERTEX_FLOATS;
        const b = indices[i + 1] * VERTEX_FLOATS;
        const c = indices[i + 2] * VERTEX_FLOATS;
        if (indices[i] < baseVertex) {
            continue; // an earlier primitive's triangle, already normalled
        }
        const ux = vertices[b] - vertices[a];
        const uy = vertices[b + 1] - vertices[a + 1];
        const uz = vertices[b + 2] - vertices[a + 2];
        const vx = vertices[c] - vertices[a];
        const vy = vertices[c + 1] - vertices[a + 1];
        const vz = vertices[c + 2] - vertices[a + 2];
        const nx = uy * vz - uz * vy;
        const ny = uz * vx - ux * vz;
        const nz = ux * vy - uy * vx;
        for (const o of [a, b, c]) {
            vertices[o + 3] += nx;
            vertices[o + 4] += ny;
            vertices[o + 5] += nz;
        }
    }
    for (let v = baseVertex * VERTEX_FLOATS; v < vertices.length; v += VERTEX_FLOATS) {
        const length = Math.hypot(vertices[v + 3], vertices[v + 4], vertices[v + 5]) || 1;
        vertices[v + 3] /= length;
        vertices[v + 4] /= length;
        vertices[v + 5] /= length;
    }
}
// --- Column-major 4x4 helpers (local to this file; Math.ts is Float32Array-based and the node
// transforms want double precision before they reach the vertex data) -------------------------
function identity() {
    const m = new Float64Array(16);
    m[0] = m[5] = m[10] = m[15] = 1;
    return m;
}
function localMatrix(node) {
    if (node.matrix && node.matrix.length === 16) {
        return Float64Array.from(node.matrix);
    }
    const [tx, ty, tz] = node.translation ?? [0, 0, 0];
    const [qx, qy, qz, qw] = node.rotation ?? [0, 0, 0, 1];
    const [sx, sy, sz] = node.scale ?? [1, 1, 1];
    // Rotation quaternion -> basis, then column-scaled and translated (T * R * S).
    const x2 = qx + qx;
    const y2 = qy + qy;
    const z2 = qz + qz;
    const xx = qx * x2;
    const xy = qx * y2;
    const xz = qx * z2;
    const yy = qy * y2;
    const yz = qy * z2;
    const zz = qz * z2;
    const wx = qw * x2;
    const wy = qw * y2;
    const wz = qw * z2;
    const m = new Float64Array(16);
    m[0] = (1 - (yy + zz)) * sx;
    m[1] = (xy + wz) * sx;
    m[2] = (xz - wy) * sx;
    m[4] = (xy - wz) * sy;
    m[5] = (1 - (xx + zz)) * sy;
    m[6] = (yz + wx) * sy;
    m[8] = (xz + wy) * sz;
    m[9] = (yz - wx) * sz;
    m[10] = (1 - (xx + yy)) * sz;
    m[12] = tx;
    m[13] = ty;
    m[14] = tz;
    m[15] = 1;
    return m;
}
function multiply(a, b) {
    const out = new Float64Array(16);
    for (let col = 0; col < 4; ++col) {
        for (let row = 0; row < 4; ++row) {
            let sum = 0;
            for (let k = 0; k < 4; ++k) {
                sum += a[k * 4 + row] * b[col * 4 + k];
            }
            out[col * 4 + row] = sum;
        }
    }
    return out;
}
function transformPoint(m, x, y, z) {
    return [
        m[0] * x + m[4] * y + m[8] * z + m[12],
        m[1] * x + m[5] * y + m[9] * z + m[13],
        m[2] * x + m[6] * y + m[10] * z + m[14],
    ];
}
function transformDirection(m, x, y, z) {
    return [
        m[0] * x + m[4] * y + m[8] * z,
        m[1] * x + m[5] * y + m[9] * z,
        m[2] * x + m[6] * y + m[10] * z,
    ];
}
function transpose(m) {
    const out = new Float64Array(16);
    for (let col = 0; col < 4; ++col) {
        for (let row = 0; row < 4; ++row) {
            out[col * 4 + row] = m[row * 4 + col];
        }
    }
    return out;
}
function invert(m) {
    const inv = new Float64Array(16);
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15]
        + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15]
        - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15]
        + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14]
        - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15]
        - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15]
        + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15]
        - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14]
        + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15]
        + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15]
        - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15]
        + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14]
        - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11]
        - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11]
        + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11]
        - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10]
        + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det === 0) {
        return identity();
    }
    for (let i = 0; i < 16; ++i) {
        inv[i] /= det;
    }
    return inv;
}
