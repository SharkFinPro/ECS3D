// Port of source/components/renderingManager/renderer3D/Renderer3D.{h,cpp}.
//
// The per-frame 3D orchestrator, with upstream's exact shape: tests submit draw requests keyed by
// PipelineType, Renderer3D groups them, renders the shadow maps, then walks the groups binding the
// right pipeline, descriptor sets and push-constant block for each.
//
// NOT PORTED: renderLine (no LinePipeline — no test drives it), doRayTracing / RayTracer /
// setCloudToRender (no WebGPU ray tracing, WebGPUPortGuide.md §1), and the display*Gui() methods —
// the GUI lives in the React layer, so tests/common/gui.ts declares those windows against the
// push-constant blocks exposed here.

import { Mat4, Vec3, mat4Invert, mat4Multiply } from "../../../utilities/Math";
import { AssetManager } from "../../assets/AssetManager";
import { SmokeSystem } from "../../assets/particleSystems/SmokeSystem";
import { RenderObject } from "../../assets/objects/RenderObject";
import { LightingManager } from "../../lighting/LightingManager";
import { LogicalDevice } from "../../logicalDevice/LogicalDevice";
import { PipelineType } from "../../pipelines/implementations/common/PipelineTypes";
import { PipelineManager } from "../../pipelines/pipelineManager/PipelineManager";
import { RenderInfo } from "../../pipelines/GraphicsPipeline";
import { SwapChain } from "../../window/SwapChain";
import { Window } from "../../window/Window";
import { MousePicker, PickFlag } from "./MousePicker";
import {
  BumpyCurtainPushConstant,
  CubeMapPushConstant,
  CurtainPushConstant,
  EllipticalDotsPushConstant,
  MagnifyWhirlMosaicPushConstant,
  NoisyEllipticalDotsPushConstant,
  defaultPushConstants,
  packPushConstants,
} from "./Renderer3DPushConstants";
import { GRID_PUSH_CONSTANT_SIZE } from "../../pipelines/pipelineManager/PipelineConfig";

// Typed view of upstream's `std::unordered_map<PipelineType, PushConstantEntry>` — a std::variant
// keyed by pipeline type becomes a mapped type, so getPushConstant() stays type-safe.
export interface PushConstantMap {
  [PipelineType.bumpyCurtain]: BumpyCurtainPushConstant;
  [PipelineType.cubeMap]: CubeMapPushConstant;
  [PipelineType.curtain]: CurtainPushConstant;
  [PipelineType.ellipticalDots]: EllipticalDotsPushConstant;
  [PipelineType.magnifyWhirlMosaic]: MagnifyWhirlMosaicPushConstant;
  [PipelineType.noisyEllipticalDots]: NoisyEllipticalDotsPushConstant;
}

// The renderObject-family pipelines, in the order they are drawn. objectHighlight is deliberately
// LAST: its shell is the same mesh scaled by 1.01, so it must alpha-blend OVER the already-shaded
// object. Drawn earlier, the closer shell wins the depth-compare "less" test and culls the object
// beneath it, leaving flat green over the background instead of a translucent tint.
const RENDER_OBJECT_PIPELINES: PipelineType[] = [
  PipelineType.object,
  PipelineType.texturedPlane,
  PipelineType.magnifyWhirlMosaic,
  PipelineType.ellipticalDots,
  PipelineType.noisyEllipticalDots,
  PipelineType.curtain,
  PipelineType.bumpyCurtain,
  PipelineType.cubeMap,
  PipelineType.objectHighlight,
];

export class Renderer3D {
  private readonly device: GPUDevice;
  private readonly mousePicker: MousePicker;
  private readonly shadowFaceDescriptorSetLayout: GPUBindGroupLayout;
  private readonly shadowFaceDescriptorSet: GPUBindGroup;

  private shouldRenderGrid = true;

  private viewPosition: Vec3 = [0, 0, 0];
  private viewMatrix: Mat4 = new Float32Array(16);

  private renderObjectsToRender = new Map<PipelineType, RenderObject[]>();
  private smokeSystemsToRender: SmokeSystem[] = [];

  // One descriptor set per RenderObject, holding its transform uniform. Created on first use and
  // reused — upstream the equivalent set lives on the RenderObject itself, but here the transform
  // is a separate group from the material (see AssetManager.ts).
  private readonly transformDescriptorSets = new Map<RenderObject, GPUBindGroup>();

  private readonly pushConstants: PushConstantMap = {
    [PipelineType.bumpyCurtain]: defaultPushConstants.bumpyCurtain(),
    [PipelineType.cubeMap]: defaultPushConstants.cubeMap(),
    [PipelineType.curtain]: defaultPushConstants.curtain(),
    [PipelineType.ellipticalDots]: defaultPushConstants.ellipticalDots(),
    [PipelineType.magnifyWhirlMosaic]: defaultPushConstants.magnifyWhirlMosaic(),
    [PipelineType.noisyEllipticalDots]: defaultPushConstants.noisyEllipticalDots(),
  };

  private readonly gridPushConstantData = new Float32Array(GRID_PUSH_CONSTANT_SIZE / 4);

  constructor(
    private readonly logicalDevice: LogicalDevice,
    private readonly assetManager: AssetManager,
    private readonly lightingManager: LightingManager,
    swapChain: SwapChain,
    window: Window,
  ) {
    this.device = logicalDevice.getDevice();
    this.mousePicker = new MousePicker(logicalDevice, swapChain, window);

    // The shadow pass reads one 80-byte slot (face view-projection + light position/far) per
    // light/face through a dynamic offset into ShadowMaps' uniform.
    this.shadowFaceDescriptorSetLayout = this.device.createBindGroupLayout({
      label: "vke.ShadowFaceDescriptorSetLayout",
      entries: [
        {
          binding: 0,
          visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
          buffer: { type: "uniform", hasDynamicOffset: true, minBindingSize: 80 },
        },
      ],
    });
    this.shadowFaceDescriptorSet = this.device.createBindGroup({
      label: "vke.ShadowFaceDescriptorSet",
      layout: this.shadowFaceDescriptorSetLayout,
      entries: [
        { binding: 0, resource: { buffer: lightingManager.getShadowMaps().faceUniformBuffer, size: 80 } },
      ],
    });
  }

  // --- Draw submission (called by tests every frame) -------------------------------------------

  renderObject(
    renderObject: RenderObject,
    pipelineType: PipelineType,
    mousePicked?: PickFlag,
  ): void {
    let objects = this.renderObjectsToRender.get(pipelineType);
    if (!objects) {
      objects = [];
      this.renderObjectsToRender.set(pipelineType, objects);
    }
    objects.push(renderObject);

    // Upload the transform once here, if it changed, rather than on every setter call.
    renderObject.flushTransform();

    this.getTransformDescriptorSet(renderObject);

    if (mousePicked) {
      this.mousePicker.renderObject(renderObject, mousePicked);
    }
  }

  renderSmokeSystem(smokeSystem: SmokeSystem): void {
    this.smokeSystemsToRender.push(smokeSystem);
  }

  getSmokeSystems(): readonly SmokeSystem[] {
    return this.smokeSystemsToRender;
  }

  // --- State -----------------------------------------------------------------------------------

  setCameraParameters(position: Vec3, viewMatrix: Mat4): void {
    this.viewPosition = position;
    this.viewMatrix = viewMatrix;
  }

  enableGrid(): void {
    this.shouldRenderGrid = true;
  }

  disableGrid(): void {
    this.shouldRenderGrid = false;
  }

  isGridEnabled(): boolean {
    return this.shouldRenderGrid;
  }

  getMousePicker(): MousePicker {
    return this.mousePicker;
  }

  getShadowFaceDescriptorSetLayout(): GPUBindGroupLayout {
    return this.shadowFaceDescriptorSetLayout;
  }

  // Mutable per-pipeline parameter block — the GUI writes straight into it, and the value is
  // packed and uploaded at draw time (bindPushConstant below).
  getPushConstant<K extends keyof PushConstantMap>(pipelineType: K): PushConstantMap[K] {
    return this.pushConstants[pipelineType];
  }

  getRenderObjectsToRender(): ReadonlyMap<PipelineType, readonly RenderObject[]> {
    return this.renderObjectsToRender;
  }

  // Analogue of Renderer3D::createNewFrame(): drop the previous frame's submissions.
  createNewFrame(): void {
    this.renderObjectsToRender = new Map();
    this.smokeSystemsToRender = [];
  }

  // --- Rendering -------------------------------------------------------------------------------

  // Six depth-only cube-face passes per shadow-casting light, before the scene pass.
  renderShadowMaps(encoder: GPUCommandEncoder, pipelineManager: PipelineManager): void {
    const shadowMaps = this.lightingManager.getShadowMaps();
    const shadowLightCount = this.lightingManager.getShadowLightCount(true);
    const objects = this.getAllRenderObjects();
    if (objects.length === 0) return;

    for (let light = 0; light < shadowLightCount; light++) {
      for (let face = 0; face < 6; face++) {
        const pass = encoder.beginRenderPass({
          label: `vke.ShadowPass.L${light}F${face}`,
          colorAttachments: [],
          depthStencilAttachment: {
            view: shadowMaps.faceView(light, face),
            depthLoadOp: "clear",
            depthStoreOp: "store",
            depthClearValue: 1,
          },
        });

        pipelineManager.bindGraphicsPipeline(pass, PipelineType.shadow);
        pipelineManager.bindGraphicsPipelineDescriptorSet(
          pass,
          PipelineType.shadow,
          this.shadowFaceDescriptorSet,
          0,
          [shadowMaps.faceUniformOffset(light, face)],
        );

        for (const object of objects) {
          pipelineManager.bindGraphicsPipelineDescriptorSet(
            pass,
            PipelineType.shadow,
            this.getTransformDescriptorSet(object),
            1,
          );
          object.draw(pass);
        }

        pass.end();
      }
    }
  }

  render(renderInfo: RenderInfo, pipelineManager: PipelineManager): void {
    this.renderRenderObjectsByPipeline(renderInfo, pipelineManager);

    if (this.shouldRenderGrid) {
      this.renderGrid(renderInfo, pipelineManager);
    }
  }

  private renderRenderObjectsByPipeline(
    renderInfo: RenderInfo,
    pipelineManager: PipelineManager,
  ): void {
    for (const pipelineType of RENDER_OBJECT_PIPELINES) {
      const objects = this.renderObjectsToRender.get(pipelineType);
      if (!objects || objects.length === 0) continue;

      this.renderRenderObjects(renderInfo, pipelineManager, pipelineType, objects);
    }
  }

  private renderRenderObjects(
    renderInfo: RenderInfo,
    pipelineManager: PipelineManager,
    pipelineType: PipelineType,
    objects: readonly RenderObject[],
  ): void {
    const { pass } = renderInfo;

    pipelineManager.bindGraphicsPipeline(pass, pipelineType);
    this.bindDescriptorSets(pipelineManager, pass, pipelineType);
    this.bindPushConstant(pipelineManager, pass, pipelineType);

    for (const object of objects) {
      // objectHighlight declares no material group, so its transform sits at group 1.
      const transformLocation = pipelineType === PipelineType.objectHighlight ? 1 : 2;

      if (pipelineType !== PipelineType.objectHighlight) {
        pipelineManager.bindGraphicsPipelineDescriptorSet(
          pass,
          pipelineType,
          object.getDescriptorSet(),
          1,
        );
      }

      pipelineManager.bindGraphicsPipelineDescriptorSet(
        pass,
        pipelineType,
        this.getTransformDescriptorSet(object),
        transformLocation,
      );

      object.draw(pass);
    }
  }

  // Every renderObject pipeline binds the lighting set at group 0.
  private bindDescriptorSets(
    pipelineManager: PipelineManager,
    pass: GPURenderPassEncoder,
    pipelineType: PipelineType,
  ): void {
    pipelineManager.bindGraphicsPipelineDescriptorSet(
      pass,
      pipelineType,
      this.lightingManager.getLightingDescriptorSet().getDescriptorSet(),
      0,
    );
  }

  // Packs and uploads this pipeline's parameter block, then binds the group holding it.
  private bindPushConstant(
    pipelineManager: PipelineManager,
    pass: GPURenderPassEncoder,
    pipelineType: PipelineType,
  ): void {
    switch (pipelineType) {
      case PipelineType.bumpyCurtain:
      case PipelineType.cubeMap:
      case PipelineType.curtain:
      case PipelineType.ellipticalDots:
      case PipelineType.magnifyWhirlMosaic:
      case PipelineType.noisyEllipticalDots: {
        const pack = packPushConstants[pipelineType] as (value: unknown) => Float32Array<ArrayBuffer>;
        pipelineManager.pushGraphicsPipelineConstants(
          pipelineType,
          pack(this.pushConstants[pipelineType]),
        );
        pipelineManager.bindGraphicsPipelinePushConstants(pass, pipelineType);
        break;
      }
      default:
        break;
    }
  }

  // Upstream's GridPushConstant carries viewProj + viewPosition; grid.wgsl additionally needs the
  // inverse (WGSL has no inverse()), so it is computed here and packed into the same block.
  private renderGrid(renderInfo: RenderInfo, pipelineManager: PipelineManager): void {
    const viewProjection = mat4Multiply(renderInfo.getProjectionMatrix(), renderInfo.viewMatrix);

    this.gridPushConstantData.set(viewProjection, 0);
    this.gridPushConstantData.set(mat4Invert(viewProjection), 16);
    this.gridPushConstantData.set(renderInfo.viewPosition, 32);

    pipelineManager.pushGraphicsPipelineConstants(PipelineType.grid, this.gridPushConstantData);
    pipelineManager.bindGraphicsPipeline(renderInfo.pass, PipelineType.grid);
    pipelineManager.bindGraphicsPipelinePushConstants(renderInfo.pass, PipelineType.grid);
    renderInfo.pass.draw(3);
  }

  getTransformDescriptorSet(object: RenderObject): GPUBindGroup {
    let descriptorSet = this.transformDescriptorSets.get(object);
    if (!descriptorSet) {
      descriptorSet = this.device.createBindGroup({
        label: "vke.TransformDescriptorSet",
        layout: this.assetManager.getTransformDescriptorSetLayout(),
        entries: [
          { binding: 0, resource: { buffer: object.getTransformUniform().getBuffer() } },
        ],
      });
      this.transformDescriptorSets.set(object, descriptorSet);
    }
    return descriptorSet;
  }

  // Every object queued this frame, regardless of pipeline — what the shadow pass renders.
  getAllRenderObjects(): RenderObject[] {
    const objects: RenderObject[] = [];
    for (const [pipelineType, queued] of this.renderObjectsToRender) {
      // The highlight shell is the same mesh scaled up; casting shadows from it would double the
      // occluder and darken the object it wraps.
      if (pipelineType === PipelineType.objectHighlight) continue;
      objects.push(...queued);
    }
    return objects;
  }

  getViewPosition(): Vec3 {
    return this.viewPosition;
  }

  getViewMatrix(): Mat4 {
    return this.viewMatrix;
  }

  getLogicalDevice(): LogicalDevice {
    return this.logicalDevice;
  }

  dispose(): void {
    this.mousePicker.dispose();
  }
}
