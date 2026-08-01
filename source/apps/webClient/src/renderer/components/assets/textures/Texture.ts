// Port of source/components/assets/textures/Texture.{h,cpp} — the base every texture kind shares.
//
// Upstream this class owns a VkImage + view + sampler and drives the staging-buffer upload and
// the layout transitions around it. In WebGPU a texture is created with usage flags and written
// with queue.writeTexture / copyExternalImageToTexture; there are no layouts, no barriers and no
// staging buffers to manage (WebGPUPortGuide.md §1, §6 AssetManager). What is left is ownership of
// the texture, its view, and the sampler it is sampled through.

export abstract class Texture {
  protected constructor(
    protected readonly texture: GPUTexture,
    protected readonly view: GPUTextureView,
    protected readonly sampler: GPUSampler,
  ) {}

  getTexture(): GPUTexture {
    return this.texture;
  }

  getImageView(): GPUTextureView {
    return this.view;
  }

  getSampler(): GPUSampler {
    return this.sampler;
  }

  destroy(): void {
    this.texture.destroy();
  }
}
