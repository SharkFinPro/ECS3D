// Port of source/components/pipelines/shaderModules/ShaderModule.{h,cpp}.
//
// Upstream reads a compiled .spv blob off disk and wraps it in a vk::ShaderModule tagged with the
// stage it feeds. WGSL ships as text and the browser compiles it, so this is a fetch plus
// createShaderModule (WebGPUPortGuide.md §5) — and there is no per-stage tagging because the
// stage is chosen by naming an entry point at pipeline-creation time.
//
// DEVIATION: upstream compiles one module per GLSL stage file (Foo.vert.spv + Foo.frag.spv). The
// ported WGSL keeps both stages in a single .wgsl module per pipeline with `vs_main` / `fs_main`
// entry points, so a GraphicsPipelineOptions names one shader file where upstream names two.

// Modules are immutable and reusable, so identical URLs are compiled once — but a GPUShaderModule
// belongs to the device that created it, and using one from a destroyed device silently produces
// an invalid pipeline. The cache is therefore keyed by device first (a WeakMap, so a torn-down
// engine's modules become collectable with it).
const moduleCache = new WeakMap<GPUDevice, Map<string, Promise<GPUShaderModule>>>();

export class ShaderModule {
  static async load(device: GPUDevice, url: string): Promise<GPUShaderModule> {
    let perDevice = moduleCache.get(device);
    if (!perDevice) {
      perDevice = new Map();
      moduleCache.set(device, perDevice);
    }

    let pending = perDevice.get(url);
    if (!pending) {
      pending = ShaderModule.compile(device, url);
      perDevice.set(url, pending);
    }
    return pending;
  }

  // Drops a device's cache so an edited .wgsl is picked up without a full reload.
  static clearCache(device: GPUDevice): void {
    moduleCache.delete(device);
  }

  private static async compile(device: GPUDevice, url: string): Promise<GPUShaderModule> {
    // no-cache: always revalidate so shader edits aren't masked by the HTTP cache.
    const response = await fetch(url, { cache: "no-cache" });
    if (!response.ok) throw new Error(`Failed to fetch shader: ${url} (${response.status})`);

    return device.createShaderModule({ label: url, code: await response.text() });
  }
}
