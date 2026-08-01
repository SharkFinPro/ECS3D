// Port of source/components/physicalDevice/PhysicalDevice.{h,cpp} — wraps the GPUAdapter.
//
// Most of the Vulkan class evaporates here (WebGPUPortGuide.md §6): device scoring, queue-family
// selection, extension checks and memory-type lookup have no WebGPU equivalent. What survives is
// the capability reporting the pipeline layer actually reads: the usable MSAA sample count, the
// depth format, and the ray-tracing capability check.

export class PhysicalDevice {
  constructor(private readonly adapter: GPUAdapter) {}

  getAdapter(): GPUAdapter {
    return this.adapter;
  }

  getLimits(): GPUSupportedLimits {
    return this.adapter.limits;
  }

  hasFeature(feature: GPUFeatureName): boolean {
    return this.adapter.features.has(feature);
  }

  // Analogue of getMaxUsableSampleCount(). Vulkan probes 64x..2x against the depth+color sample
  // count limits; WebGPU guarantees support for exactly 1 and 4 and offers nothing above
  // (WebGPUPortGuide.md §6 SwapChain, gotcha 4), so "max usable" is always 4.
  getMsaaSamples(): number {
    return 4;
  }

  // Analogue of findDepthFormat(). Vulkan probes eD32Sfloat/eD32SfloatS8Uint/eD24UnormS8Uint for
  // optimal-tiling depth-stencil support; every WebGPU implementation supports depth24plus, so
  // there is no format bake-off to run.
  findDepthFormat(): GPUTextureFormat {
    return "depth24plus";
  }

  // Always false: no WebGPU ray-tracing extension has shipped (WebGPUPortGuide.md §1). Kept so
  // the engine keeps the same raster-fallback branch upstream takes on non-RT hardware.
  supportsRayTracing(): boolean {
    return false;
  }
}
