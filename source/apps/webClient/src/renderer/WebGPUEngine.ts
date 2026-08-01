// Port of source/VulkanEngine.{h,cpp} — the engine facade.
//
// Owns and wires every subsystem in the same order upstream does (device -> assets -> lighting ->
// rendering -> pipelines -> camera), exposes the same getters, and drives one frame per render()
// call. Construction is async because adapter/device requests and shader fetches are
// (WebGPUPortGuide.md §3.5), so `static create()` stands in for the constructor.
//
// DEVIATION: `while (renderer.isActive()) { ...; renderer.render(); }` cannot exist in a browser —
// the tab would hang (§2). The loop inverts: each test returns a per-frame function and the test
// runner drives it from requestAnimationFrame, calling render() at the end of each frame.

import { DeepPartial, EngineConfig, resolveEngineConfig } from "./EngineConfig";
import { AssetManager } from "./components/assets/AssetManager";
import { Camera } from "./components/camera/Camera";
import { ImGuiInstance } from "./components/imGui/ImGuiInstance";
import { LightingManager } from "./components/lighting/LightingManager";
import { LogicalDevice } from "./components/logicalDevice/LogicalDevice";
import { PipelineManager } from "./components/pipelines/pipelineManager/PipelineManager";
import { RenderingManager } from "./components/renderingManager/RenderingManager";
import { SwapChain } from "./components/window/SwapChain";
import { Window } from "./components/window/Window";

const DEFAULT_FONT = "Roboto";

export class WebGPUEngine {
  private currentFrame = 0;

  private constructor(
    private readonly window: Window,
    private readonly logicalDevice: LogicalDevice,
    private readonly swapChain: SwapChain,
    private readonly assetManager: AssetManager,
    private readonly lightingManager: LightingManager,
    private readonly renderingManager: RenderingManager,
    private readonly pipelineManager: PipelineManager,
    private readonly camera: Camera,
    private readonly imGuiInstance: ImGuiInstance,
  ) {}

  static async create(
    canvas: HTMLCanvasElement,
    config?: DeepPartial<EngineConfig>,
  ): Promise<WebGPUEngine> {
    const engineConfig = resolveEngineConfig(config);

    // initializeVulkanAndWindow()
    const window = new Window(canvas, engineConfig.window);
    const logicalDevice = await LogicalDevice.create();
    const swapChain = new SwapChain(logicalDevice, window);

    // createComponents()
    const assetManager = new AssetManager(logicalDevice);
    const lightingManager = new LightingManager(logicalDevice.getDevice());
    const renderingManager = new RenderingManager(
      logicalDevice,
      swapChain,
      window,
      assetManager,
      lightingManager,
    );
    const pipelineManager = await PipelineManager.create(
      logicalDevice,
      swapChain,
      renderingManager,
      lightingManager,
      assetManager,
    );

    // The 2D renderer needs its typeface before any text() call; loading it here keeps tests from
    // having to know about it (upstream bundles fonts under source/assets the same way).
    renderingManager.getRenderer2D().setFont(await assetManager.getFont(DEFAULT_FONT));

    // createCamera()
    const camera = new Camera(engineConfig.camera);

    return new WebGPUEngine(
      window,
      logicalDevice,
      swapChain,
      assetManager,
      lightingManager,
      renderingManager,
      pipelineManager,
      camera,
      new ImGuiInstance(),
    );
  }

  isActive(): boolean {
    return this.window.isOpen();
  }

  // One frame. Upstream this is the body after the test's submissions inside the while loop.
  render(timeMs: number = performance.now()): void {
    this.window.update();

    if (this.camera.isEnabled()) {
      this.camera.processInput(this.window, timeMs);
    }

    this.renderingManager.doRendering(this.pipelineManager, this.camera, this.currentFrame);

    this.createNewFrame();
    this.currentFrame = 1 - this.currentFrame;
  }

  getAssetManager(): AssetManager {
    return this.assetManager;
  }

  getCamera(): Camera {
    return this.camera;
  }

  getImGuiInstance(): ImGuiInstance {
    return this.imGuiInstance;
  }

  getLightingManager(): LightingManager {
    return this.lightingManager;
  }

  getRenderingManager(): RenderingManager {
    return this.renderingManager;
  }

  getPipelineManager(): PipelineManager {
    return this.pipelineManager;
  }

  getWindow(): Window {
    return this.window;
  }

  getLogicalDevice(): LogicalDevice {
    return this.logicalDevice;
  }

  getSwapChain(): SwapChain {
    return this.swapChain;
  }

  // Index of the frame's ping-pong slot, used by the smoke systems' double-buffered particles.
  getCurrentFrame(): number {
    return this.currentFrame;
  }

  private createNewFrame(): void {
    this.renderingManager.createNewFrame();
  }

  dispose(): void {
    this.renderingManager.dispose();
    this.window.dispose();
    this.swapChain.destroy();
    this.logicalDevice.destroy();
  }
}
