// Port of source/components/pipelines/ComputePipeline.{h,cpp}.
//
// A trivial mapping (WebGPUPortGuide.md §1): same shader module, same layout, and the dispatch is
// recorded into a compute pass on the frame's encoder instead of a separately-submitted command
// buffer — there is only one queue, so ComputingManager's cross-queue ordering disappears.

import { LogicalDevice } from "../logicalDevice/LogicalDevice";
import { Pipeline, PushConstantRange } from "./Pipeline";
import { ShaderModule } from "./shaderModules/ShaderModule";

export interface ComputePipelineOptions {
  label: string;
  shaders: {
    shader: string;
    computeEntryPoint?: string; // default "cs_main"
  };
  pushConstantRanges?: PushConstantRange[];
  bindGroupLayouts?: GPUBindGroupLayout[];
}

export class ComputePipeline extends Pipeline {
  private computePipeline!: GPUComputePipeline;

  private constructor() {
    super();
  }

  static async create(
    logicalDevice: LogicalDevice,
    options: ComputePipelineOptions,
  ): Promise<ComputePipeline> {
    const device = logicalDevice.getDevice();
    const module = await ShaderModule.load(device, options.shaders.shader);

    const computePipeline = new ComputePipeline();
    computePipeline.createPipelineLayout(device, options);
    computePipeline.createPipeline(device, module, options);

    return computePipeline;
  }

  get pipeline(): GPUComputePipeline {
    return this.computePipeline;
  }

  bind(pass: GPUComputePassEncoder): void {
    pass.setPipeline(this.computePipeline);
  }

  protected createPipelineLayout(device: GPUDevice, options: ComputePipelineOptions): void {
    const bindGroupLayouts = [...(options.bindGroupLayouts ?? [])];

    const pushConstantLayout = this.createPushConstantLayout(
      device,
      options.label,
      options.pushConstantRanges,
      bindGroupLayouts.length,
    );
    if (pushConstantLayout) bindGroupLayouts.push(pushConstantLayout);

    this.pipelineLayout = device.createPipelineLayout({
      label: `${options.label}.Layout`,
      bindGroupLayouts,
    });
  }

  protected createPipeline(
    device: GPUDevice,
    module: GPUShaderModule,
    options: ComputePipelineOptions,
  ): void {
    this.computePipeline = device.createComputePipeline({
      label: options.label,
      layout: this.pipelineLayout,
      compute: { module, entryPoint: options.shaders.computeEntryPoint ?? "cs_main" },
    });
  }
}
