// Port of source/components/pipelines/implementations/common/GraphicsPipelineStates.h.
//
// The same named presets the upstream PipelineConfig files compose, translated field-for-field
// (WebGPUPortGuide.md §6, PipelineManager row). Import it namespaced so call sites read like the
// C++ ones:
//
//   import * as gps from "../implementations/common/GraphicsPipelineStates";
//   ... .colorBlendState = gps.colorBlendState
//
// Dropped presets, all because the concept does not exist in WebGPU:
//   - viewportState / dynamicState — viewport and scissor are always dynamic, never pipeline state.
//   - rasterizationStateOutline — there is no polygonMode; wireframe needs a line-list topology.
//   - inputAssemblyStatePointList — no point primitives; smoke draws instanced billboard quads
//     instead (see SmokeParticle.ts).
//   - getMultsampleStateAlpha — alpha-to-coverage exists in WebGPU, but the ported 2D renderer
//     does not enable it (its shaders already alpha-blend), so the preset would be unused.

import { LogicalDevice } from "../../../logicalDevice/LogicalDevice";
import { Vertex } from "../vertexInputs/Vertex";

// ---------------------------------------------------------------------------------------------
// State types. WebGPU groups these differently from Vulkan's nine PipelineXCreateInfo structs, so
// each preset carries just the fields GraphicsPipeline needs to fill a GPURenderPipelineDescriptor.
// ---------------------------------------------------------------------------------------------

export interface ColorBlendState {
  // 0 = no color attachment at all (Vulkan's attachmentCount = 0).
  attachmentCount: number;
  blend?: GPUBlendState;
  // Left unset by every preset below. Upstream spells out R|G|B|A on each attachment, which is
  // exactly WebGPU's default — and naming it would mean evaluating the `GPUColorWrite` global at
  // module scope, which throws during Next's server-side render (these presets are module-level
  // constants). Only set it for a pipeline that genuinely masks channels.
  writeMask?: GPUColorWriteFlags;
}

export interface DepthStencilState {
  depthWriteEnabled: boolean;
  depthCompare: GPUCompareFunction;
}

export interface InputAssemblyState {
  topology: GPUPrimitiveTopology;
}

export interface MultisampleState {
  count: number;
}

export interface RasterizationState {
  cullMode: GPUCullMode;
  frontFace: GPUFrontFace;
}

export type VertexInputState = GPUVertexBufferLayout[];

// ---------------------------------------------------------------------------------------------
// Color blend
// ---------------------------------------------------------------------------------------------

export const colorBlendState: ColorBlendState = {
  attachmentCount: 1,
};

export const colorBlendStateDots: ColorBlendState = {
  attachmentCount: 1,
  blend: {
    color: { srcFactor: "src-alpha", dstFactor: "one-minus-src-alpha", operation: "add" },
    alpha: { srcFactor: "src-alpha", dstFactor: "one-minus-src-alpha", operation: "add" },
  },
};

export const colorBlendStateSmoke: ColorBlendState = {
  attachmentCount: 1,
  blend: {
    color: { srcFactor: "src-alpha", dstFactor: "one-minus-src-alpha", operation: "add" },
    alpha: { srcFactor: "src-alpha", dstFactor: "one", operation: "add" },
  },
};

export const colorBlendStateBendy: ColorBlendState = {
  attachmentCount: 1,
  blend: {
    color: { srcFactor: "src-alpha", dstFactor: "one-minus-src-alpha", operation: "add" },
    alpha: { srcFactor: "one", dstFactor: "zero", operation: "add" },
  },
};

// DEVIATION: upstream's colorBlendStateTransparent leaves the alpha channel as One/Zero, which is
// what an offscreen target wants. This port draws straight into the swapchain, so the destination
// alpha has to survive the blend for the composited image to stay opaque — hence One /
// OneMinusSrcAlpha. Used by the grid and the 2D primitives (upstream: colorBlendStateDots and
// colorBlendStateTransparent respectively).
export const colorBlendStateTransparent: ColorBlendState = {
  attachmentCount: 1,
  blend: {
    color: { srcFactor: "src-alpha", dstFactor: "one-minus-src-alpha", operation: "add" },
    alpha: { srcFactor: "one", dstFactor: "one-minus-src-alpha", operation: "add" },
  },
};

export const colorBlendStateShadow: ColorBlendState = {
  attachmentCount: 0,
};

// ---------------------------------------------------------------------------------------------
// Depth / stencil
// ---------------------------------------------------------------------------------------------

export const depthStencilState: DepthStencilState = {
  depthWriteEnabled: true,
  depthCompare: "less",
};

// Tests against the scene depth but leaves it untouched, so translucent overlays (the grid) do
// not occlude anything drawn after them.
export const depthStencilStateNoWrite: DepthStencilState = {
  depthWriteEnabled: false,
  depthCompare: "less",
};

// Skybox: sits exactly on the far plane, so it needs LessEqual to survive its own depth clear.
export const depthStencilStateSkybox: DepthStencilState = {
  depthWriteEnabled: false,
  depthCompare: "less-equal",
};

export const depthStencilStateNone: DepthStencilState = {
  depthWriteEnabled: false,
  depthCompare: "always",
};

// ---------------------------------------------------------------------------------------------
// Input assembly
// ---------------------------------------------------------------------------------------------

export const inputAssemblyStateTriangleList: InputAssemblyState = { topology: "triangle-list" };
export const inputAssemblyStateTriangleStrip: InputAssemblyState = { topology: "triangle-strip" };
export const inputAssemblyStateLineList: InputAssemblyState = { topology: "line-list" };

// ---------------------------------------------------------------------------------------------
// Multisample
// ---------------------------------------------------------------------------------------------

// Upstream spells this getMultsampleState (sic).
export function getMultisampleState(logicalDevice: LogicalDevice): MultisampleState {
  return { count: logicalDevice.getPhysicalDevice().getMsaaSamples() };
}

export const multisampleStateNone: MultisampleState = { count: 1 };

// ---------------------------------------------------------------------------------------------
// Rasterization
// ---------------------------------------------------------------------------------------------

export const rasterizationStateCullBack: RasterizationState = {
  cullMode: "back",
  frontFace: "ccw",
};

export const rasterizationStateNoCull: RasterizationState = {
  cullMode: "none",
  frontFace: "ccw",
};

// ---------------------------------------------------------------------------------------------
// Vertex input
// ---------------------------------------------------------------------------------------------

export const vertexInputStateRaw: VertexInputState = [];

export const vertexInputStateVertex: VertexInputState = [
  { ...Vertex.getBindingDescription(), attributes: Vertex.getAttributeDescriptions() },
];

export const vertexInputStateVertexPositionOnly: VertexInputState = [
  { ...Vertex.getBindingDescription(), attributes: Vertex.getAttributeDescriptionsPositionOnly() },
];

export const vertexInputStateVertexPositionAndNormal: VertexInputState = [
  {
    ...Vertex.getBindingDescription(),
    attributes: Vertex.getAttributeDescriptionsPositionAndNormal(),
  },
];
