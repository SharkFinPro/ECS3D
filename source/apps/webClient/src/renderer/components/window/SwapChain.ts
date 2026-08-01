// Port of source/components/window/SwapChain.{h,cpp} — the canvas's WebGPU context.
//
// Structurally the same object: it owns the presentable images plus the MSAA color and depth
// image resources the render pass attaches (upstream m_colorImageResources / m_depthImageResources),
// and it is recreated on resize. What disappears (WebGPUPortGuide.md §1): surface-format and
// present-mode selection, image-count negotiation, explicit image views per swapchain image, and
// every layout transition — `configure()` covers all of it and `getCurrentTexture()` hands back
// the image for the frame.

import { LogicalDevice } from "../logicalDevice/LogicalDevice";
import { Window } from "./Window";

export class SwapChain {
  private readonly canvasContext: GPUCanvasContext;
  private readonly imageFormat: GPUTextureFormat;
  private readonly viewFormat: GPUTextureFormat;
  private readonly sampleCount: number;

  // MSAA color + depth attachments, sized to the swapchain extent.
  private colorImageResource: GPUTexture | null = null;
  private depthImageResource: GPUTexture | null = null;

  private extent = { width: 1, height: 1 };
  private configured = false;

  constructor(
    private readonly logicalDevice: LogicalDevice,
    private readonly window: Window,
  ) {
    const canvasContext = window.getCanvas().getContext("webgpu");
    if (!canvasContext) throw new Error("Failed to get a WebGPU canvas context.");
    this.canvasContext = canvasContext;

    // Matches chooseSwapSurfaceFormat, which picks eR8G8B8A8Unorm with an eSrgbNonlinear COLOUR
    // SPACE. That distinction matters: the colour space only tells the display how to interpret the
    // bytes, it does not make the GPU gamma-encode on write. Upstream therefore does no colour
    // conversion anywhere in the chain — shader output lands in the framebuffer verbatim, and
    // textures are sampled verbatim (Texture2D is eR8G8B8A8Unorm too).
    //
    // So the render target must NOT be an `-srgb` view. Using one makes the GPU apply a
    // linear->sRGB encode on every write, which lifts 0.5 to 0.73 and makes the whole image —
    // including untextured geometry like the grid — visibly brighter than the Vulkan build.
    this.imageFormat = navigator.gpu.getPreferredCanvasFormat();
    this.viewFormat = this.imageFormat;
    this.sampleCount = logicalDevice.getPhysicalDevice().getMsaaSamples();

    this.recreate(true);
  }

  getImageFormat(): GPUTextureFormat {
    return this.imageFormat;
  }

  // The format every pipeline's color target must declare. Kept as a separate accessor from
  // getImageFormat() because WebGPU spells sRGB as a view format — but see the constructor: this
  // port deliberately keeps it equal to the image format so no gamma encode happens on write.
  getViewFormat(): GPUTextureFormat {
    return this.viewFormat;
  }

  getSampleCount(): number {
    return this.sampleCount;
  }

  getExtent(): { width: number; height: number } {
    return this.extent;
  }

  getAspect(): number {
    return this.extent.width / Math.max(1, this.extent.height);
  }

  // The presentable image for this frame — the scene pass's resolve target.
  getSurfaceView(): GPUTextureView {
    return this.canvasContext.getCurrentTexture().createView({ format: this.viewFormat });
  }

  getCurrentTexture(): GPUTexture {
    return this.canvasContext.getCurrentTexture();
  }

  // The multisampled color attachment the scene pass renders into.
  getColorView(): GPUTextureView {
    if (!this.colorImageResource) throw new Error("SwapChain is not configured");
    return this.colorImageResource.createView();
  }

  getDepthView(): GPUTextureView {
    if (!this.depthImageResource) throw new Error("SwapChain is not configured");
    return this.depthImageResource.createView();
  }

  // Analogue of RenderingManager::recreateSwapChain() — minus the waitIdle and the
  // eErrorOutOfDateKHR dance, neither of which exists in WebGPU. Returns true when anything
  // actually changed. `force` re-binds the canvas to this device even at an unchanged size,
  // which is needed to win React StrictMode's double-mount race.
  recreate(force = false): boolean {
    const canvas = this.window.getCanvas();
    const scale = this.window.getContentScale();
    const width = Math.max(1, Math.floor(canvas.clientWidth * scale));
    const height = Math.max(1, Math.floor(canvas.clientHeight * scale));

    if (!force && this.configured && this.extent.width === width && this.extent.height === height) {
      return false;
    }

    const device = this.logicalDevice.getDevice();
    canvas.width = width;
    canvas.height = height;
    this.extent = { width, height };

    this.canvasContext.configure({
      device,
      format: this.imageFormat,
      viewFormats: [this.viewFormat],
      alphaMode: "opaque",
    });
    this.configured = true;

    this.createImageResources(device);
    return true;
  }

  destroy(): void {
    this.colorImageResource?.destroy();
    this.depthImageResource?.destroy();
    this.colorImageResource = null;
    this.depthImageResource = null;
  }

  private createImageResources(device: GPUDevice): void {
    const { width, height } = this.extent;
    const physicalDevice = this.logicalDevice.getPhysicalDevice();

    this.colorImageResource?.destroy();
    this.colorImageResource = device.createTexture({
      label: "vke.SwapChain.ColorImageResource",
      size: { width, height },
      format: this.viewFormat,
      sampleCount: this.sampleCount,
      usage: GPUTextureUsage.RENDER_ATTACHMENT,
    });

    this.depthImageResource?.destroy();
    this.depthImageResource = device.createTexture({
      label: "vke.SwapChain.DepthImageResource",
      size: { width, height },
      format: physicalDevice.findDepthFormat(),
      sampleCount: this.sampleCount,
      usage: GPUTextureUsage.RENDER_ATTACHMENT,
    });
  }
}
