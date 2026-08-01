// Port of source/components/assets/objects/Model.{h,cpp} — a GPU-resident mesh.
//
// Upstream loads through Assimp, which handles both .obj and .glb. Assimp has no browser build
// here, so this carries a minimal Wavefront OBJ reader plus a minimal glTF 2.0 reader (GlbLoader.ts)
// and dispatches on the extension.
//
// ECS3D CHANGE: the webGPUTest original supported .obj only and substituted procedural meshes for
// the two .glb test assets. Every ECS3D asset is .glb and the AssetRegistry can name a new one at
// runtime, so the loader is required rather than optional.

import { loadGlb } from "./GlbLoader";

const VERTEX_FLOATS = 8; // position(3) normal(3) texCoord(2) — see vertexInputs/Vertex.ts

interface Mesh {
  vertices: Float32Array<ArrayBuffer>;
  indices: Uint32Array<ArrayBuffer>;
}

export class Model {
  private constructor(
    readonly vertexBuffer: GPUBuffer,
    readonly indexBuffer: GPUBuffer,
    readonly indexCount: number,
  ) {}

  static async load(device: GPUDevice, url: string): Promise<Model> {
    const isBinaryGltf = url.split("?")[0].toLowerCase().endsWith(".glb");
    const mesh = isBinaryGltf ? await loadGlb(url) : await loadObj(url);

    return Model.create(device, mesh.vertices, mesh.indices, url);
  }

  // Uploads an already-assembled mesh. Used by the OBJ path above and by ProceduralGeometry.
  static create(
    device: GPUDevice,
    vertices: Float32Array<ArrayBuffer>,
    indices: Uint32Array<ArrayBuffer>,
    label: string,
  ): Model {
    const vertexBuffer = device.createBuffer({
      label: `vke.VertexBuffer(${label})`,
      size: vertices.byteLength,
      usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
    });
    device.queue.writeBuffer(vertexBuffer, 0, vertices);

    const indexBuffer = device.createBuffer({
      label: `vke.IndexBuffer(${label})`,
      size: indices.byteLength,
      usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST,
    });
    device.queue.writeBuffer(indexBuffer, 0, indices);

    return new Model(vertexBuffer, indexBuffer, indices.length);
  }

  destroy(): void {
    this.vertexBuffer.destroy();
    this.indexBuffer.destroy();
  }
}

// UV v is flipped: the OBJ origin is bottom-left, WebGPU's texture origin is top-left.
async function loadObj(url: string): Promise<Mesh> {
  const response = await fetch(url);
  if (!response.ok) throw new Error(`Failed to fetch model: ${url} (${response.status})`);
  const text = await response.text();

  const positions: number[][] = [];
  const uvs: number[][] = [];
  const normals: number[][] = [];
  const vertices: number[] = [];
  const indices: number[] = [];
  const cache = new Map<string, number>();

  const addVertex = (spec: string): number => {
    const cached = cache.get(spec);
    if (cached !== undefined) return cached;

    const [vi, ti, ni] = spec
      .split("/")
      .map((s) => (s === "" || s === undefined ? 0 : parseInt(s, 10)));
    const p = positions[vi < 0 ? positions.length + vi : vi - 1] ?? [0, 0, 0];
    const t = ti ? (uvs[ti < 0 ? uvs.length + ti : ti - 1] ?? [0, 0]) : [0, 0];
    const n = ni ? (normals[ni < 0 ? normals.length + ni : ni - 1] ?? [0, 1, 0]) : [0, 0, 0];

    const index = vertices.length / VERTEX_FLOATS;
    vertices.push(p[0], p[1], p[2], n[0], n[1], n[2], t[0], 1 - t[1]);
    cache.set(spec, index);
    return index;
  };

  for (const line of text.split("\n")) {
    const parts = line.trim().split(/\s+/);
    switch (parts[0]) {
      case "v":
        positions.push([+parts[1], +parts[2], +parts[3]]);
        break;
      case "vt":
        uvs.push([+parts[1], +parts[2]]);
        break;
      case "vn":
        normals.push([+parts[1], +parts[2], +parts[3]]);
        break;
      case "f": {
        const face = parts.slice(1).map(addVertex);
        for (let i = 1; i + 1 < face.length; i++) {
          indices.push(face[0], face[i], face[i + 1]);
        }
        break;
      }
    }
  }

  const mesh: Mesh = { vertices: new Float32Array(vertices), indices: new Uint32Array(indices) };
  if (normals.length === 0) computeNormals(mesh);
  return mesh;
}

// Area-weighted vertex normals, for OBJs that ship without any.
function computeNormals(mesh: Mesh): void {
  const { vertices, indices } = mesh;
  for (let i = 0; i < indices.length; i += 3) {
    const [a, b, c] = [
      indices[i] * VERTEX_FLOATS,
      indices[i + 1] * VERTEX_FLOATS,
      indices[i + 2] * VERTEX_FLOATS,
    ];
    const ux = vertices[b] - vertices[a],
      uy = vertices[b + 1] - vertices[a + 1],
      uz = vertices[b + 2] - vertices[a + 2];
    const vx = vertices[c] - vertices[a],
      vy = vertices[c + 1] - vertices[a + 1],
      vz = vertices[c + 2] - vertices[a + 2];
    const nx = uy * vz - uz * vy,
      ny = uz * vx - ux * vz,
      nz = ux * vy - uy * vx;
    for (const o of [a, b, c]) {
      vertices[o + 3] += nx;
      vertices[o + 4] += ny;
      vertices[o + 5] += nz;
    }
  }
  for (let v = 0; v < vertices.length; v += VERTEX_FLOATS) {
    const l = Math.hypot(vertices[v + 3], vertices[v + 4], vertices[v + 5]) || 1;
    vertices[v + 3] /= l;
    vertices[v + 4] /= l;
    vertices[v + 5] /= l;
  }
}
