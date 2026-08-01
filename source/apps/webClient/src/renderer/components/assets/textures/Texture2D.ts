// Port of source/components/assets/textures/Texture2D.{h,cpp}.
//
// stb_image + a staging buffer + two layout transitions collapse into fetch → createImageBitmap →
// copyExternalImageToTexture. Mipmaps are not generated: WebGPU has no blit chain, so upstream's
// generateMipmaps() would need the ~100-line render-downsample pass the port guide describes
// (§6 AssetManager) — none of the ported tests are mip-limited, so it is left out.

import { Texture } from "./Texture";

export class Texture2D extends Texture {
  private constructor(texture: GPUTexture, view: GPUTextureView, sampler: GPUSampler) {
    super(texture, view, sampler);
  }

  // No sRGB variant: upstream creates every Texture2D as eR8G8B8A8Unorm, so sampling returns the
  // stored bytes verbatim with no gamma decode. Pairing an `-srgb` texture with this port's
  // non-sRGB render target would darken every textured surface relative to the Vulkan build.
  static async load(
    device: GPUDevice,
    url: string,
    sampler: GPUSampler,
  ): Promise<Texture2D> {
    const response = await fetch(url);
    if (!response.ok) throw new Error(`Failed to fetch texture: ${url} (${response.status})`);

    // colorSpaceConversion "none": use the authored pixels, no browser colour management.
    const bitmap = await createImageBitmap(await response.blob(), { colorSpaceConversion: "none" });

    const texture = device.createTexture({
      label: url,
      size: { width: bitmap.width, height: bitmap.height },
      format: "rgba8unorm",
      usage:
        GPUTextureUsage.TEXTURE_BINDING |
        GPUTextureUsage.COPY_DST |
        GPUTextureUsage.RENDER_ATTACHMENT,
    });

    device.queue.copyExternalImageToTexture(
      { source: bitmap },
      { texture },
      { width: bitmap.width, height: bitmap.height },
    );
    bitmap.close();

    return new Texture2D(texture, texture.createView(), sampler);
  }

  // The sampler every material texture is read through — upstream Texture::createSampler(), with
  // the same linear/linear/repeat settings.
  static createSampler(device: GPUDevice): GPUSampler {
    return device.createSampler({
      label: "vke.MaterialSampler",
      magFilter: "linear",
      minFilter: "linear",
      mipmapFilter: "linear",
      addressModeU: "repeat",
      addressModeV: "repeat",
    });
  }
}
