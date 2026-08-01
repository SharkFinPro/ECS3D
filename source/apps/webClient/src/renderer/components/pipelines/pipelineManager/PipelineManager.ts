// Port of source/components/pipelines/pipelineManager/PipelineManager.{h,cpp}.
//
// The single place pipelines are built and owned, keyed by PipelineType, exactly as upstream —
// this is what replaced the port's previous arrangement, where every pipeline was its own class
// that hand-rolled its bind group layouts, its uniform buffers and its own draw method.
//
// The command pool and descriptor pool upstream creates here are gone (WebGPU has neither).
// Creation is async because the WGSL modules are fetched; upstream's constructor becomes
// `static create()`, and the create*Pipelines() helpers keep their upstream names and grouping.
//
// DEVIATION: upstream builds BendyPipeline / SmokePipeline / DotsPipeline / LinePipeline eagerly
// in createMiscPipelines(). The ported expansion pipelines (bendy, crosses, snake) need buffers
// that only a scene can supply — a source-triangle mesh, per-plant model matrices — so they are
// created on demand through the create*Pipeline() methods below and owned here from then on.
// DotsPipeline and LinePipeline are not ported: no test drives them.

import { AssetManager } from "../../assets/AssetManager";
import { LightingManager } from "../../lighting/LightingManager";
import { LogicalDevice } from "../../logicalDevice/LogicalDevice";
import { RenderingManager } from "../../renderingManager/RenderingManager";
import { SwapChain } from "../../window/SwapChain";
import { GraphicsPipeline } from "../GraphicsPipeline";
import { PipelineType } from "../implementations/common/PipelineTypes";
import * as PipelineConfig from "./PipelineConfig";
import * as PipelineConfig2D from "./PipelineConfig2D";
import * as PipelineConfigRenderObject from "./PipelineConfigRenderObject";
import { RenderObjectLayouts } from "./PipelineConfigRenderObject";

export class PipelineManager {
  private readonly graphicsPipelines = new Map<PipelineType, GraphicsPipeline>();

  private constructor(
    private readonly logicalDevice: LogicalDevice,
    private readonly swapChain: SwapChain,
  ) {}

  static async create(
    logicalDevice: LogicalDevice,
    swapChain: SwapChain,
    renderingManager: RenderingManager,
    lightingManager: LightingManager,
    assetManager: AssetManager,
  ): Promise<PipelineManager> {
    const pipelineManager = new PipelineManager(logicalDevice, swapChain);
    await pipelineManager.createPipelines(assetManager, renderingManager, lightingManager);
    return pipelineManager;
  }

  bindGraphicsPipeline(pass: GPURenderPassEncoder, pipelineType: PipelineType): void {
    this.getGraphicsPipeline(pipelineType).bind(pass);
  }

  // Analogue of pushGraphicsPipelineConstants<T>(commandBuffer, type, stageFlags, offset, data).
  // No stage flags: visibility is fixed by the pipeline's pushConstantRanges (Pipeline.ts).
  pushGraphicsPipelineConstants(
    pipelineType: PipelineType,
    data: BufferSource,
    offset = 0,
  ): void {
    this.getGraphicsPipeline(pipelineType).pushConstants(
      this.logicalDevice.getQueue(),
      data,
      offset,
    );
  }

  // Binds the group holding a pipeline's push-constant block. Upstream needs no equivalent: a
  // Vulkan push constant is bound implicitly by vkCmdPushConstants.
  bindGraphicsPipelinePushConstants(
    pass: GPURenderPassEncoder,
    pipelineType: PipelineType,
  ): void {
    this.getGraphicsPipeline(pipelineType).bindPushConstants(pass);
  }

  bindGraphicsPipelineDescriptorSet(
    pass: GPURenderPassEncoder,
    pipelineType: PipelineType,
    descriptorSet: GPUBindGroup,
    location: number,
    dynamicOffsets?: number[],
  ): void {
    this.getGraphicsPipeline(pipelineType).bindBindGroup(
      pass,
      descriptorSet,
      location,
      dynamicOffsets,
    );
  }

  hasPipeline(pipelineType: PipelineType): boolean {
    return this.graphicsPipelines.has(pipelineType);
  }

  private async createGraphicsPipeline(
    pipelineType: PipelineType,
    options: Parameters<typeof GraphicsPipeline.create>[2],
  ): Promise<void> {
    this.graphicsPipelines.set(
      pipelineType,
      await GraphicsPipeline.create(this.logicalDevice, this.swapChain, options),
    );
  }

  private async createPipelines(
    assetManager: AssetManager,
    renderingManager: RenderingManager,
    lightingManager: LightingManager,
  ): Promise<void> {
    await this.create2DPipelines(assetManager, renderingManager);

    await this.createRenderObjectPipelines(assetManager, renderingManager, lightingManager);

    await this.createMiscPipelines();
  }

  private async create2DPipelines(
    assetManager: AssetManager,
    renderingManager: RenderingManager,
  ): Promise<void> {
    const screenLayout = renderingManager.getRenderer2D().getScreenDescriptorSetLayout();

    await this.createGraphicsPipeline(
      PipelineType.rect,
      PipelineConfig2D.createRectPipelineOptions(this.logicalDevice, screenLayout),
    );

    await this.createGraphicsPipeline(
      PipelineType.triangle,
      PipelineConfig2D.createTrianglePipelineOptions(this.logicalDevice, screenLayout),
    );

    await this.createGraphicsPipeline(
      PipelineType.ellipse,
      PipelineConfig2D.createEllipsePipelineOptions(this.logicalDevice, screenLayout),
    );

    await this.createGraphicsPipeline(
      PipelineType.font,
      PipelineConfig2D.createFontPipelineOptions(
        this.logicalDevice,
        screenLayout,
        assetManager.getFontDescriptorSetLayout(),
      ),
    );
  }

  private async createRenderObjectPipelines(
    assetManager: AssetManager,
    renderingManager: RenderingManager,
    lightingManager: LightingManager,
  ): Promise<void> {
    const layouts: RenderObjectLayouts = {
      lighting: lightingManager.getLightingDescriptorSet().getDescriptorSetLayout(),
      object: assetManager.getObjectDescriptorSetLayout(),
      transform: assetManager.getTransformDescriptorSetLayout(),
    };

    const renderer3D = renderingManager.getRenderer3D();
    const noise = assetManager.getNoiseTexture();
    const cubeMap = await assetManager.getCubeMapTexture();

    await this.createGraphicsPipeline(
      PipelineType.object,
      PipelineConfigRenderObject.createObjectsPipelineOptions(this.logicalDevice, layouts),
    );

    await this.createGraphicsPipeline(
      PipelineType.objectHighlight,
      PipelineConfigRenderObject.createObjectHighlightPipelineOptions(this.logicalDevice, layouts),
    );

    await this.createGraphicsPipeline(
      PipelineType.ellipticalDots,
      PipelineConfigRenderObject.createEllipticalDotsPipelineOptions(this.logicalDevice, layouts),
    );

    await this.createGraphicsPipeline(
      PipelineType.noisyEllipticalDots,
      PipelineConfigRenderObject.createNoisyEllipticalDotsPipelineOptions(
        this.logicalDevice,
        layouts,
        noise.getImageView(),
        noise.getSampler(),
      ),
    );

    await this.createGraphicsPipeline(
      PipelineType.bumpyCurtain,
      PipelineConfigRenderObject.createBumpyCurtainPipelineOptions(
        this.logicalDevice,
        layouts,
        noise.getImageView(),
        noise.getSampler(),
      ),
    );

    await this.createGraphicsPipeline(
      PipelineType.curtain,
      PipelineConfigRenderObject.createCurtainPipelineOptions(this.logicalDevice, layouts),
    );

    await this.createGraphicsPipeline(
      PipelineType.cubeMap,
      PipelineConfigRenderObject.createCubeMapPipelineOptions(
        this.logicalDevice,
        layouts,
        noise.getImageView(),
        noise.getSampler(),
        cubeMap.getImageView(),
        cubeMap.getSampler(),
      ),
    );

    await this.createGraphicsPipeline(
      PipelineType.texturedPlane,
      PipelineConfigRenderObject.createTexturedPlanePipelineOptions(this.logicalDevice, layouts),
    );

    await this.createGraphicsPipeline(
      PipelineType.magnifyWhirlMosaic,
      PipelineConfigRenderObject.createMagnifyWhirlMosaicPipelineOptions(
        this.logicalDevice,
        layouts,
      ),
    );

    await this.createGraphicsPipeline(
      PipelineType.shadow,
      PipelineConfigRenderObject.createShadowMapPipelineOptions(
        layouts,
        renderer3D.getShadowFaceDescriptorSetLayout(),
      ),
    );

    await this.createGraphicsPipeline(
      PipelineType.mousePicking,
      PipelineConfigRenderObject.createMousePickingPipelineOptions(
        layouts,
        renderer3D.getMousePicker().getPickDescriptorSetLayout(),
      ),
    );
  }

  private async createMiscPipelines(): Promise<void> {
    await this.createGraphicsPipeline(
      PipelineType.grid,
      PipelineConfig.createGridPipelineOptions(this.logicalDevice),
    );
  }

  private getGraphicsPipeline(pipelineType: PipelineType): GraphicsPipeline {
    const pipeline = this.graphicsPipelines.get(pipelineType);
    if (!pipeline) {
      throw new Error(`Pipeline for the given type does not exist: ${pipelineType}`);
    }
    return pipeline;
  }
}
