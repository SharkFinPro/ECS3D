// PORT-ONLY FILE — no upstream counterpart.
//
// Model.ts loads Wavefront OBJ only (no Assimp in the browser), so the two GLB assets the tests
// reference are substituted with procedural meshes matching their real extents, read out of each
// GLB's JSON chunk:
//   square.glb  = a +/-1 cube node-scaled [10,1,10]  -> buildBox()   : a 20 x 2 x 20 slab
//   curtain.glb = a +/-5 vertical plane              -> buildPlane() : a subdivided quad
//
// The subdivision in buildPlane also gives the Curtain vertex displacement and the
// noise-perturbed variants enough resolution to look right.

import { Model } from "./Model";

// Matches curtain.glb: a +/-1 XZ plane node-rotated +90 degrees about X and scaled 5, i.e. normal
// +Z, u along +X, image top at +Y. v is flipped so image-space top (v = 0) lands on the top edge.
export function buildPlane(
  device: GPUDevice,
  segments = 64,
  halfSize = 5,
  label = "plane",
): Model {
  const vertices: number[] = [];
  const indices: number[] = [];

  for (let y = 0; y <= segments; y++) {
    for (let x = 0; x <= segments; x++) {
      const u = x / segments;
      const v = y / segments;
      const px = (u - 0.5) * 2 * halfSize;
      const py = (v - 0.5) * 2 * halfSize;
      // position(3), normal(3) = +Z, uv(2)
      vertices.push(px, py, 0, 0, 0, 1, u, 1 - v);
    }
  }

  const stride = segments + 1;
  for (let y = 0; y < segments; y++) {
    for (let x = 0; x < segments; x++) {
      const a = y * stride + x;
      const b = a + 1;
      const c = a + stride;
      const d = c + 1;
      indices.push(a, c, b, b, c, d);
    }
  }

  return Model.create(device, new Float32Array(vertices), new Uint32Array(indices), label);
}

// Stand-in for square.glb. Six quad faces, each with an outward normal and the full [0,1] UV
// range (Blender-cube style), oriented so image-space top is up on the side faces.
export function buildBox(
  device: GPUDevice,
  halfExtents: [number, number, number] = [10, 1, 10],
  label = "box",
): Model {
  const [hx, hy, hz] = halfExtents;
  const vertices: number[] = [];
  const indices: number[] = [];

  // Each face: normal n, right axis r (u+), up axis t (v- toward image top).
  const faces: {
    n: [number, number, number];
    r: [number, number, number];
    t: [number, number, number];
  }[] = [
    { n: [1, 0, 0], r: [0, 0, 1], t: [0, 1, 0] }, // +X
    { n: [-1, 0, 0], r: [0, 0, -1], t: [0, 1, 0] }, // -X
    { n: [0, 1, 0], r: [1, 0, 0], t: [0, 0, -1] }, // +Y (top)
    { n: [0, -1, 0], r: [1, 0, 0], t: [0, 0, 1] }, // -Y (bottom)
    { n: [0, 0, 1], r: [-1, 0, 0], t: [0, 1, 0] }, // +Z
    { n: [0, 0, -1], r: [1, 0, 0], t: [0, 1, 0] }, // -Z
  ];

  faces.forEach((f, fi) => {
    for (let corner = 0; corner < 4; corner++) {
      const u = corner % 2;
      const v = corner >> 1; // v = 0 top row, v = 1 bottom row
      const px = f.n[0] * hx + (u * 2 - 1) * f.r[0] * hx + (1 - v * 2) * f.t[0] * hx;
      const py = f.n[1] * hy + (u * 2 - 1) * f.r[1] * hy + (1 - v * 2) * f.t[1] * hy;
      const pz = f.n[2] * hz + (u * 2 - 1) * f.r[2] * hz + (1 - v * 2) * f.t[2] * hz;
      vertices.push(px, py, pz, ...f.n, u, v);
    }

    // Wind each face CCW as seen from outside so back-face-culling consumers (mouse picking and
    // object highlight both cull back, like a real square.glb) draw every face. The base order
    // (b, b+2, b+1)/(b+1, b+2, b+3) is CCW in the (r, t) basis, which is outward-facing only when
    // r x t == +n; the four side faces have r x t == -n, so flip their winding.
    const cross: [number, number, number] = [
      f.r[1] * f.t[2] - f.r[2] * f.t[1],
      f.r[2] * f.t[0] - f.r[0] * f.t[2],
      f.r[0] * f.t[1] - f.r[1] * f.t[0],
    ];
    const outward = cross[0] * f.n[0] + cross[1] * f.n[1] + cross[2] * f.n[2] > 0;
    const b = fi * 4;
    if (outward) {
      indices.push(b, b + 2, b + 1, b + 1, b + 2, b + 3);
    } else {
      indices.push(b, b + 1, b + 2, b + 1, b + 3, b + 2);
    }
  });

  return Model.create(device, new Float32Array(vertices), new Uint32Array(indices), label);
}
