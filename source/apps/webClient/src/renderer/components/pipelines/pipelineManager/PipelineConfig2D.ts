// Port of source/components/pipelines/pipelineManager/PipelineConfig2D.h.
//
// Same four pipelines as upstream (rect / triangle / ellipse / font) with the same states. Two
// differences, both from Primitives2D.ts's instanced-payload deviation:
//  - there are no pushConstantRanges; the per-primitive block is an instance vertex buffer, so
//    `vertexInputState` carries what upstream's push-constant range carried.
//  - all three shape pipelines come from ONE module (twoD.wgsl) selected by entry point, where
//    upstream compiles a separate .vert/.frag pair per primitive.
//
// DEVIATION: upstream uses getMultsampleStateAlpha (alpha-to-coverage) for 2D. These shaders
// already alpha-blend, so plain MSAA is used and the sample count simply has to match the scene
// pass they draw into.

import { LogicalDevice } from "../../logicalDevice/LogicalDevice";
import { GraphicsPipelineOptions } from "../GraphicsPipeline";
import * as gps from "../implementations/common/GraphicsPipelineStates";
import {
  instanceLayoutGlyph,
  instanceLayoutRectEllipse,
  instanceLayoutTriangle,
} from "../../renderingManager/renderer2D/Primitives2D";

const SHAPE_SHADER = "/shaders/twoD.wgsl";
const TEXT_SHADER = "/shaders/twoDText.wgsl";

export function createRectPipelineOptions(
  logicalDevice: LogicalDevice,
  screenDescriptorSetLayout: GPUBindGroupLayout,
): GraphicsPipelineOptions {
  return {
    label: "vke.RectPipeline",
    shaders: {
      shader: SHAPE_SHADER,
      vertexEntryPoint: "vs_rect",
      fragmentEntryPoint: "fs_rect",
    },
    states: {
      colorBlendState: gps.colorBlendStateTransparent,
      // DEVIATION: the 2D overlay draws last with its own painter-order depth key, so it neither
      // tests nor writes the scene depth (upstream relies on depthStencilState here).
      depthStencilState: gps.depthStencilStateNone,
      inputAssemblyState: gps.inputAssemblyStateTriangleStrip,
      multisampleState: gps.getMultisampleState(logicalDevice),
      rasterizationState: gps.rasterizationStateNoCull,
      vertexInputState: [instanceLayoutRectEllipse],
    },
    bindGroupLayouts: [screenDescriptorSetLayout],
  };
}

export function createTrianglePipelineOptions(
  logicalDevice: LogicalDevice,
  screenDescriptorSetLayout: GPUBindGroupLayout,
): GraphicsPipelineOptions {
  return {
    label: "vke.TrianglePipeline",
    shaders: {
      shader: SHAPE_SHADER,
      vertexEntryPoint: "vs_triangle",
      fragmentEntryPoint: "fs_triangle",
    },
    states: {
      colorBlendState: gps.colorBlendStateTransparent,
      depthStencilState: gps.depthStencilStateNone,
      inputAssemblyState: gps.inputAssemblyStateTriangleList,
      multisampleState: gps.getMultisampleState(logicalDevice),
      rasterizationState: gps.rasterizationStateNoCull,
      vertexInputState: [instanceLayoutTriangle],
    },
    bindGroupLayouts: [screenDescriptorSetLayout],
  };
}

export function createEllipsePipelineOptions(
  logicalDevice: LogicalDevice,
  screenDescriptorSetLayout: GPUBindGroupLayout,
): GraphicsPipelineOptions {
  return {
    label: "vke.EllipsePipeline",
    shaders: {
      shader: SHAPE_SHADER,
      vertexEntryPoint: "vs_ellipse",
      fragmentEntryPoint: "fs_ellipse",
    },
    states: {
      colorBlendState: gps.colorBlendStateTransparent,
      depthStencilState: gps.depthStencilStateNone,
      inputAssemblyState: gps.inputAssemblyStateTriangleStrip,
      multisampleState: gps.getMultisampleState(logicalDevice),
      rasterizationState: gps.rasterizationStateNoCull,
      vertexInputState: [instanceLayoutRectEllipse],
    },
    bindGroupLayouts: [screenDescriptorSetLayout],
  };
}

export function createFontPipelineOptions(
  logicalDevice: LogicalDevice,
  screenDescriptorSetLayout: GPUBindGroupLayout,
  fontDescriptorSetLayout: GPUBindGroupLayout,
): GraphicsPipelineOptions {
  return {
    label: "vke.FontPipeline",
    shaders: {
      shader: TEXT_SHADER,
    },
    states: {
      colorBlendState: gps.colorBlendStateTransparent,
      depthStencilState: gps.depthStencilStateNone,
      inputAssemblyState: gps.inputAssemblyStateTriangleStrip,
      multisampleState: gps.getMultisampleState(logicalDevice),
      rasterizationState: gps.rasterizationStateNoCull,
      vertexInputState: [instanceLayoutGlyph],
    },
    bindGroupLayouts: [screenDescriptorSetLayout, fontDescriptorSetLayout],
  };
}
