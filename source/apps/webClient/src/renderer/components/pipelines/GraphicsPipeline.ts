// Port of source/components/pipelines/GraphicsPipeline.h — RenderInfo, GraphicsPipelineOptions
// and the GraphicsPipeline class itself.
//
// GraphicsPipelineOptions keeps the upstream shape (shaders / states / pushConstantRanges /
// layouts / formats) so a PipelineConfig entry reads the same on both sides. Three differences,
// all forced by the API:
//
//  1. `shaders` names ONE .wgsl module instead of a .vert + .frag pair — WGSL keeps both stages in
//     one file, selected by entry point (see ShaderModule.ts).
//  2. `descriptorSetLayouts` is spelled `bindGroupLayouts` (WebGPUPortGuide.md §1: a
//     vk::DescriptorSetLayout *is* a wgpu::BindGroupLayout).
//  3. Creation is async, because the WGSL source is fetched at runtime rather than baked in as
//     SPIR-V at build time. Hence `static create()` rather than a constructor.

import { Mat4, Vec3, mat4Perspective } from "../../utilities/Math";
import { LogicalDevice } from "../logicalDevice/LogicalDevice";
import { SwapChain } from "../window/SwapChain";
import { Pipeline, PushConstantGroupBindings, PushConstantRange } from "./Pipeline";
import { ShaderModule } from "./shaderModules/ShaderModule";
import {
  ColorBlendState,
  DepthStencilState,
  InputAssemblyState,
  MultisampleState,
  RasterizationState,
  VertexInputState,
} from "./implementations/common/GraphicsPipelineStates";

// Port of the RenderInfo struct. `commandBuffer` becomes the render pass encoder, since WebGPU
// records draws into a pass rather than into the command buffer directly.
export class RenderInfo {
  private projectionMatrix: Mat4 | null = null;

  constructor(
    readonly pass: GPURenderPassEncoder,
    readonly currentFrame: number,
    readonly viewPosition: Vec3,
    readonly viewMatrix: Mat4,
    readonly extent: { width: number; height: number },
  ) {}

  // Same 45 degrees / 0.1 / 1000 projection, built once per RenderInfo. The upstream
  // `projectionMatrix[1][1] *= -1` clip-space flip is omitted: WebGPU's NDC already has +Y up.
  getProjectionMatrix(): Mat4 {
    if (!this.projectionMatrix) {
      this.projectionMatrix = mat4Perspective(
        (45 * Math.PI) / 180,
        this.extent.width / Math.max(1, this.extent.height),
        0.1,
        1000.0,
      );
    }
    return this.projectionMatrix;
  }
}

export interface GraphicsPipelineOptions {
  label: string;

  shaders: {
    shader: string;
    vertexEntryPoint?: string; // default "vs_main"
    fragmentEntryPoint?: string; // default "fs_main"
  };

  states: {
    colorBlendState: ColorBlendState;
    depthStencilState: DepthStencilState;
    inputAssemblyState: InputAssemblyState;
    multisampleState: MultisampleState;
    rasterizationState: RasterizationState;
    vertexInputState: VertexInputState;
  };

  pushConstantRanges?: PushConstantRange[];

  // Per-effect textures/samplers that share the push-constant bind group (see Pipeline.ts).
  pushConstantGroupBindings?: PushConstantGroupBindings;

  bindGroupLayouts?: GPUBindGroupLayout[];

  // Defaults to the swapchain's view format. Upstream defaults to the offscreen target's
  // eR8G8B8A8Unorm; this port draws straight into the swapchain, so the default comes from there.
  // Pipelines rendering elsewhere (mouse picking's rgba8uint target) set it explicitly.
  colorFormat?: GPUTextureFormat;

  // Defaults to PhysicalDevice::findDepthFormat(). Pipelines rendering to another depth target
  // (shadow maps) must set it explicitly, exactly as upstream requires.
  depthFormat?: GPUTextureFormat;
}

export class GraphicsPipeline extends Pipeline {
  private renderPipeline!: GPURenderPipeline;

  private constructor() {
    super();
  }

  static async create(
    logicalDevice: LogicalDevice,
    swapChain: SwapChain,
    options: GraphicsPipelineOptions,
  ): Promise<GraphicsPipeline> {
    const device = logicalDevice.getDevice();
    const module = await ShaderModule.load(device, options.shaders.shader);

    const graphicsPipeline = new GraphicsPipeline();

    // Error scopes plus the `label` on every descriptor are what replace the Vulkan validation
    // layers and DebugMessenger here (WebGPUPortGuide.md §1). Scoping creation means a bad
    // pipeline reports the descriptor that caused it, instead of surfacing later as an opaque
    // "invalid pipeline" at draw time.
    device.pushErrorScope("validation");
    graphicsPipeline.createPipelineLayout(device, options);
    graphicsPipeline.createPipeline(device, logicalDevice, swapChain, module, options);
    const error = await device.popErrorScope();
    if (error) {
      throw new Error(`Failed to create pipeline "${options.label}": ${error.message}`);
    }

    return graphicsPipeline;
  }

  get pipeline(): GPURenderPipeline {
    return this.renderPipeline;
  }

  bind(pass: GPURenderPassEncoder): void {
    pass.setPipeline(this.renderPipeline);
  }

  // Analogue of GraphicsPipeline::bindDescriptorSet(commandBuffer, descriptorSet, location).
  bindBindGroup(
    pass: GPURenderPassEncoder,
    bindGroup: GPUBindGroup,
    location: number,
    dynamicOffsets?: number[],
  ): void {
    // setBindGroup is overloaded, and the three-argument form requires a real sequence — passing
    // an explicit `undefined` throws "The provided value cannot be converted to a sequence" on
    // stricter implementations. Only most sets use dynamic offsets, so pick the overload.
    if (dynamicOffsets) {
      pass.setBindGroup(location, bindGroup, dynamicOffsets);
    } else {
      pass.setBindGroup(location, bindGroup);
    }
  }

  protected createPipelineLayout(device: GPUDevice, options: GraphicsPipelineOptions): void {
    const bindGroupLayouts = [...(options.bindGroupLayouts ?? [])];

    // No push constants in WebGPU: the block becomes a uniform in the group after the declared
    // ones (Pipeline.ts).
    const pushConstantLayout = this.createPushConstantLayout(
      device,
      options.label,
      options.pushConstantRanges,
      bindGroupLayouts.length,
      options.pushConstantGroupBindings,
    );
    if (pushConstantLayout) bindGroupLayouts.push(pushConstantLayout);

    this.pipelineLayout = device.createPipelineLayout({
      label: `${options.label}.Layout`,
      bindGroupLayouts,
    });
  }

  protected createPipeline(
    device: GPUDevice,
    logicalDevice: LogicalDevice,
    swapChain: SwapChain,
    module: GPUShaderModule,
    options: GraphicsPipelineOptions,
  ): void {
    const { states } = options;

    // attachmentCount 0 is upstream's colorBlendStateShadow: a depth-only pass. The fragment
    // stage still runs (the shadow shader writes frag_depth), it just has no color target.
    const targets: GPUColorTargetState[] =
      states.colorBlendState.attachmentCount === 0
        ? []
        : [
            {
              format: options.colorFormat ?? swapChain.getViewFormat(),
              blend: states.colorBlendState.blend,
              writeMask: states.colorBlendState.writeMask,
            },
          ];

    this.renderPipeline = device.createRenderPipeline({
      label: options.label,
      layout: this.pipelineLayout,
      vertex: {
        module,
        entryPoint: options.shaders.vertexEntryPoint ?? "vs_main",
        buffers: states.vertexInputState,
      },
      fragment: {
        module,
        entryPoint: options.shaders.fragmentEntryPoint ?? "fs_main",
        targets,
      },
      primitive: {
        topology: states.inputAssemblyState.topology,
        cullMode: states.rasterizationState.cullMode,
        frontFace: states.rasterizationState.frontFace,
      },
      depthStencil: {
        format: options.depthFormat ?? logicalDevice.getPhysicalDevice().findDepthFormat(),
        depthWriteEnabled: states.depthStencilState.depthWriteEnabled,
        depthCompare: states.depthStencilState.depthCompare,
      },
      multisample: { count: states.multisampleState.count },
    });
  }
}
