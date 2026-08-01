// Port of source/components/assets/textures/Texture3D.{h,cpp} — the 3D noise volume sampled by
// the NoisyEllipticalDots, BumpyCurtain and CubeMap fragment shaders.
//
// DEVIATION: upstream loads source/assets/noise/noise3d.064.tex, a precomputed 64^3 RGBA volume
// whose four channels hold gradient-noise octaves (each ~0.5 mean, so r+g+b+a ~= 2.0 and the
// shaders' `n = r+g+b+a - 2.0` yields a signed perturbation in ~[-1,1]). That binary asset is not
// in this repo, so an equivalent is synthesized here: four independent trilinearly-interpolated
// value-noise octaves (lattice frequencies 4/8/16/32), one per channel. The mean-per-channel
// contract the shaders depend on is preserved; the exact texel values differ.

import { Texture } from "./Texture";

const SIZE = 64;

export class Texture3D extends Texture {
  private constructor(texture: GPUTexture, view: GPUTextureView, sampler: GPUSampler) {
    super(texture, view, sampler);
  }

  static createNoise(device: GPUDevice, label = "vke.Noise3D"): Texture3D {
    const data = new Uint8Array(SIZE * SIZE * SIZE * 4);
    valueNoiseChannel(data, 0, 4, 11);
    valueNoiseChannel(data, 1, 8, 29);
    valueNoiseChannel(data, 2, 16, 53);
    valueNoiseChannel(data, 3, 32, 97);

    const texture = device.createTexture({
      label,
      dimension: "3d",
      size: { width: SIZE, height: SIZE, depthOrArrayLayers: SIZE },
      format: "rgba8unorm",
      usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
    });

    device.queue.writeTexture(
      { texture },
      data,
      { bytesPerRow: SIZE * 4, rowsPerImage: SIZE },
      { width: SIZE, height: SIZE, depthOrArrayLayers: SIZE },
    );

    return new Texture3D(
      texture,
      texture.createView({ dimension: "3d" }),
      Texture3D.createSampler(device),
    );
  }

  static createSampler(device: GPUDevice): GPUSampler {
    return device.createSampler({
      label: "vke.NoiseSampler",
      magFilter: "linear",
      minFilter: "linear",
      addressModeU: "repeat",
      addressModeV: "repeat",
      addressModeW: "repeat",
    });
  }
}

// Deterministic hash -> [0,1). Keeps the texture identical across runs/reloads.
function hash(x: number, y: number, z: number, seed: number): number {
  let h = x * 374761393 + y * 668265263 + z * 2147483647 + seed * 972897143;
  h = (h ^ (h >>> 13)) >>> 0;
  h = Math.imul(h, 1274126177) >>> 0;
  return (h & 0xffffff) / 0x1000000;
}

function smooth(t: number): number {
  return t * t * (3 - 2 * t);
}

// Tiling trilinear value noise sampled at SIZE^3, lattice period `freq`.
function valueNoiseChannel(out: Uint8Array, offset: number, freq: number, seed: number): void {
  const cell = SIZE / freq;
  for (let z = 0; z < SIZE; z++) {
    for (let y = 0; y < SIZE; y++) {
      for (let x = 0; x < SIZE; x++) {
        const fx = x / cell, fy = y / cell, fz = z / cell;
        const x0 = Math.floor(fx), y0 = Math.floor(fy), z0 = Math.floor(fz);
        const tx = smooth(fx - x0), ty = smooth(fy - y0), tz = smooth(fz - z0);
        const x1 = (x0 + 1) % freq, y1 = (y0 + 1) % freq, z1 = (z0 + 1) % freq;
        const wx0 = x0 % freq, wy0 = y0 % freq, wz0 = z0 % freq;

        const c000 = hash(wx0, wy0, wz0, seed), c100 = hash(x1, wy0, wz0, seed);
        const c010 = hash(wx0, y1, wz0, seed), c110 = hash(x1, y1, wz0, seed);
        const c001 = hash(wx0, wy0, z1, seed), c101 = hash(x1, wy0, z1, seed);
        const c011 = hash(wx0, y1, z1, seed), c111 = hash(x1, y1, z1, seed);

        const x00 = c000 + (c100 - c000) * tx, x10 = c010 + (c110 - c010) * tx;
        const x01 = c001 + (c101 - c001) * tx, x11 = c011 + (c111 - c011) * tx;
        const y0v = x00 + (x10 - x00) * ty, y1v = x01 + (x11 - x01) * ty;
        const v = y0v + (y1v - y0v) * tz;

        out[(z * SIZE * SIZE + y * SIZE + x) * 4 + offset] = Math.round(v * 255);
      }
    }
  }
}
