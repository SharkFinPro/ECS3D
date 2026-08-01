// Port of source/components/assets/textures/TextureCubemap.{h,cpp}.
//
// A cubemap is a 6-layer 2D texture with a `cube` view — fully supported, and without the
// per-face staging copies and layout transitions upstream needs (WebGPUPortGuide.md §1).
// Layer order is the standard +X,-X,+Y,-Y,+Z,-Z so sampling by direction matches CubeMap.frag.

import { Texture } from "./Texture";

const FACE_FILES = [
  "nvposx.bmp", // +X (layer 0)
  "nvnegx.bmp", // -X (layer 1)
  "nvposy.bmp", // +Y (layer 2)
  "nvnegy.bmp", // -Y (layer 3)
  "nvposz.bmp", // +Z (layer 4)
  "nvnegz.bmp", // -Z (layer 5)
];

export class TextureCubemap extends Texture {
  private constructor(texture: GPUTexture, view: GPUTextureView, sampler: GPUSampler) {
    super(texture, view, sampler);
  }

  static async load(
    device: GPUDevice,
    basePath = "/assets/cubeMap",
    label = "vke.RoomCubeMap",
  ): Promise<TextureCubemap> {
    const bitmaps = await Promise.all(
      FACE_FILES.map(async (file) => {
        const response = await fetch(`${basePath}/${file}`);
        if (!response.ok) throw new Error(`Failed to fetch cube face: ${file} (${response.status})`);
        return createImageBitmap(await response.blob(), { colorSpaceConversion: "none" });
      }),
    );

    const size = bitmaps[0].width;

    // eR8G8B8A8Unorm, matching TextureCubemap.cpp — no gamma decode on sample, like every other
    // texture in this engine.
    const texture = device.createTexture({
      label,
      dimension: "2d",
      size: { width: size, height: size, depthOrArrayLayers: 6 },
      format: "rgba8unorm",
      usage:
        GPUTextureUsage.TEXTURE_BINDING |
        GPUTextureUsage.COPY_DST |
        GPUTextureUsage.RENDER_ATTACHMENT,
    });

    bitmaps.forEach((bitmap, layer) => {
      device.queue.copyExternalImageToTexture(
        { source: bitmap },
        { texture, origin: { x: 0, y: 0, z: layer } },
        { width: bitmap.width, height: bitmap.height },
      );
      bitmap.close();
    });

    return new TextureCubemap(
      texture,
      texture.createView({ label: `${label}.View`, dimension: "cube" }),
      TextureCubemap.createSampler(device),
    );
  }

  static createSampler(device: GPUDevice): GPUSampler {
    return device.createSampler({
      label: "vke.CubeMapSampler",
      magFilter: "linear",
      minFilter: "linear",
      addressModeU: "clamp-to-edge",
      addressModeV: "clamp-to-edge",
      addressModeW: "clamp-to-edge",
    });
  }
}
