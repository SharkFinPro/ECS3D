// Port of source/components/assets/textures/TextureGlyph.{h,cpp} — the single-channel texture a
// glyph is sampled from (upstream's FreeType bitmap uploaded as an R8 image; here r8unorm, which
// is exactly the format the port guide calls out for it, §6 AssetManager).
//
// DEVIATION: upstream allocates one TextureGlyph per glyph. Rasterizing 95 separate WebGPU
// textures per font size would be wasteful, so this holds ONE atlas texture for a whole size and
// Font.ts stores each glyph's UV rect into it. Font.frag samples the same way either way — the
// only difference is that the UVs are a sub-rect rather than the full [0,1] range.

import { Texture } from "./Texture";

export class TextureGlyph extends Texture {
  private constructor(
    texture: GPUTexture,
    view: GPUTextureView,
    sampler: GPUSampler,
    readonly width: number,
    readonly height: number,
  ) {
    super(texture, view, sampler);
  }

  // `alpha` is one byte of coverage per texel, row-major, tightly packed.
  static createAtlas(
    device: GPUDevice,
    label: string,
    alpha: Uint8Array<ArrayBuffer>,
    width: number,
    height: number,
    sampler: GPUSampler,
  ): TextureGlyph {
    const texture = device.createTexture({
      label,
      size: { width, height },
      format: "r8unorm",
      usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
    });

    device.queue.writeTexture(
      { texture },
      alpha,
      { bytesPerRow: width, rowsPerImage: height },
      { width, height },
    );

    return new TextureGlyph(texture, texture.createView(), sampler, width, height);
  }

  static createSampler(device: GPUDevice): GPUSampler {
    return device.createSampler({
      label: "vke.GlyphSampler",
      magFilter: "linear",
      minFilter: "linear",
      addressModeU: "clamp-to-edge",
      addressModeV: "clamp-to-edge",
    });
  }
}
