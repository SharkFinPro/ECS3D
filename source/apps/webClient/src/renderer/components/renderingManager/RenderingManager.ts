// Port of source/components/renderingManager/RenderingManager.{h,cpp}.
//
// Owns the per-frame work and the two renderers, as upstream does. What disappears
// (WebGPUPortGuide.md §3): FrameScheduler and its timeline semaphore, the separate offscreen and
// swapchain command buffers, every image barrier, and the eErrorOutOfDateKHR/resize dance —
// WebGPU submissions execute in order and the surface is simply reconfigured on resize.
//
// DEVIATION: upstream renders the scene into an offscreen RenderTarget, shows it inside an ImGui
// "Scene View" dock, and then blits it to the swapchain with the offscreenToSwapchain pipeline.
// This port's GUI is React outside the canvas, so the scene renders straight into the swapchain
// and both the offscreen target and the blit pipeline are gone.

import { Mat4, Vec3 } from "../../utilities/Math";
import { AssetManager } from "../assets/AssetManager";
import { Camera } from "../camera/Camera";
import { LightingManager } from "../lighting/LightingManager";
import { LogicalDevice } from "../logicalDevice/LogicalDevice";
import { RenderInfo } from "../pipelines/GraphicsPipeline";
import { PipelineManager } from "../pipelines/pipelineManager/PipelineManager";
import { SwapChain } from "../window/SwapChain";
import { FramebufferResizeEvent, Window } from "../window/Window";
import { Renderer2D } from "./renderer2D/Renderer2D";
import { Renderer3D } from "./renderer3D/Renderer3D";

export class RenderingManager {
  private readonly renderer2D: Renderer2D;
  private readonly renderer3D: Renderer3D;

  private shadowsEnabled = true;

  // Escape hatch for the pipelines that cannot be built at engine startup because they need
  // buffers only a scene can supply (smoke, bendy, crosses, snake, skybox — see PipelineManager's
  // deviation note). Upstream reaches these through PipelineManager's named render*Pipeline
  // methods; here a test registers a callback per frame. Cleared every frame.
  private customDraws: ((renderInfo: RenderInfo) => void)[] = [];
  private customComputes: ((pass: GPUComputePassEncoder) => void)[] = [];

  constructor(
    private readonly logicalDevice: LogicalDevice,
    private readonly swapChain: SwapChain,
    window: Window,
    assetManager: AssetManager,
    private readonly lightingManager: LightingManager,
  ) {
    this.renderer2D = new Renderer2D(logicalDevice, assetManager.getFontDescriptorSetLayout());
    this.renderer3D = new Renderer3D(
      logicalDevice,
      assetManager,
      lightingManager,
      swapChain,
      window,
    );

    // Upstream's FramebufferResizeEvent listener; reconfiguring is the whole of recreateSwapChain.
    window.on("framebufferResize", (_event: FramebufferResizeEvent) => {
      this.recreateSwapChain();
    });
  }

  getRenderer2D(): Renderer2D {
    return this.renderer2D;
  }

  getRenderer3D(): Renderer3D {
    return this.renderer3D;
  }

  getSwapChain(): SwapChain {
    return this.swapChain;
  }

  // Always false: no WebGPU ray-tracing extension has shipped. Kept so tests can branch on it the
  // way upstream does on non-RT hardware.
  supportsRayTracing(): boolean {
    return this.logicalDevice.getPhysicalDevice().supportsRayTracing();
  }

  setShadowsEnabled(enabled: boolean): void {
    this.shadowsEnabled = enabled;
  }

  areShadowsEnabled(): boolean {
    return this.shadowsEnabled;
  }

  renderCustom(draw: (renderInfo: RenderInfo) => void): void {
    this.customDraws.push(draw);
  }

  // Scene-owned compute work, recorded into one compute pass BEFORE the shadow and scene passes in
  // the same encoder — so a draw later in the frame sees its results for free (WebGPU executes
  // passes in submit order).
  renderCustomCompute(compute: (pass: GPUComputePassEncoder) => void): void {
    this.customComputes.push(compute);
  }

  recreateSwapChain(): boolean {
    return this.swapChain.recreate();
  }

  createNewFrame(): void {
    this.renderer3D.createNewFrame();
    this.lightingManager.clearLightsToRender();
  }

  // Port of RenderingManager::doRendering(). One encoder, three phases in submit order: scene
  // compute, shadow cube faces, then the scene pass; mouse picking is appended last.
  doRendering(pipelineManager: PipelineManager, camera: Camera, currentFrame: number): void {
    const device = this.logicalDevice.getDevice();
    const queue = this.logicalDevice.getQueue();
    const extent = this.swapChain.getExtent();

    // ECS3D CHANGE: only the free-fly camera pushes its pose here, and only while it is enabled.
    // Upstream C++ has the same guard (RenderingManager::render pushes the free-fly pose only while
    // the scene view is focused); webGPUTest dropped it because every test drives the fly camera. Once
    // a scene Camera component takes over, RenderSystem.updateCamera disables the fly camera and calls
    // setCameraParameters itself - pushing unconditionally here overwrote that every frame, and since
    // a disabled fly camera never runs processInput, the view froze at its configured start position.
    if (camera.isEnabled()) {
      this.renderer3D.setCameraParameters(camera.getPosition(), camera.getViewMatrix());
    }

    // Read back from Renderer3D rather than the fly camera, so lighting, shadows and the projection
    // all use whichever camera is actually in charge this frame.
    const viewPosition: Vec3 = this.renderer3D.getViewPosition();
    const viewMatrix: Mat4 = this.renderer3D.getViewMatrix();

    const shadowLightCount = this.lightingManager.getShadowLightCount(this.shadowsEnabled);

    // A RenderInfo without a pass, purely to get the frame's projection matrix; the real one below
    // carries the pass encoder. (Upstream builds one RenderInfo because the projection is not
    // needed before the pass begins.)
    const projectionMatrix = new RenderInfo(
      undefined as unknown as GPURenderPassEncoder,
      currentFrame,
      viewPosition,
      viewMatrix,
      extent,
    ).getProjectionMatrix();

    this.lightingManager.update(queue, viewMatrix, projectionMatrix, viewPosition, shadowLightCount);

    const encoder = device.createCommandEncoder({ label: "vke.FrameEncoder" });

    if (this.customComputes.length > 0) {
      const computePass = encoder.beginComputePass({ label: "vke.ComputePass" });
      for (const compute of this.customComputes) {
        compute(computePass);
      }
      computePass.end();
      this.customComputes = [];
    }

    if (shadowLightCount > 0) {
      this.renderer3D.renderShadowMaps(encoder, pipelineManager);
    }

    const pass = encoder.beginRenderPass({
      label: "vke.ScenePass",
      colorAttachments: [
        {
          // Render into the multisampled color target and resolve into the swapchain texture — the
          // WebGPU equivalent of the Vulkan MSAA color image plus its eAverage resolve attachment.
          // storeOp "discard": the MSAA samples are never read after the resolve.
          view: this.swapChain.getColorView(),
          resolveTarget: this.swapChain.getSurfaceView(),
          loadOp: "clear",
          storeOp: "discard",
          // Matches the upstream SwapChain clear colour.
          clearValue: { r: 0, g: 0, b: 0, a: 1 },
        },
      ],
      depthStencilAttachment: {
        view: this.swapChain.getDepthView(),
        depthLoadOp: "clear",
        depthStoreOp: "store",
        depthClearValue: 1,
      },
    });

    const renderInfo = new RenderInfo(pass, currentFrame, viewPosition, viewMatrix, extent);

    this.renderer3D.render(renderInfo, pipelineManager);

    for (const draw of this.customDraws) {
      draw(renderInfo);
    }
    this.customDraws = [];

    // The 2D overlay draws last, on top of the scene.
    this.renderer2D.render(renderInfo, pipelineManager);

    pass.end();

    // Object-ID pass plus the cursor readback copy, encoded after the scene pass so it sees the
    // same per-object uniforms. Resolved after submit (WebGPUPortGuide.md §3.3).
    const mousePicker = this.renderer3D.getMousePicker();
    const readback = mousePicker.render(
      encoder,
      pipelineManager,
      this.lightingManager.getLightingDescriptorSet().getDescriptorSet(),
      (object) => this.renderer3D.getTransformDescriptorSet(object),
    );

    queue.submit([encoder.finish()]);

    if (readback) mousePicker.handleRenderedMousePickingImage();
  }

  dispose(): void {
    this.renderer3D.dispose();
  }
}
