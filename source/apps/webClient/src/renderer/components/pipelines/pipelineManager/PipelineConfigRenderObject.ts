// Port of source/components/pipelines/pipelineManager/PipelineConfigRenderObject.h.
//
// One factory per renderObject-family PipelineType, each returning a fully-declared
// GraphicsPipelineOptions — the same shape, order and state presets as upstream, so the two files
// diff almost line for line.
//
// Bind group mapping (upstream set index -> WebGPU group):
//   group 0 = lighting descriptor set (camera + lights + shadow cube)  <- LightingManager
//   group 1 = object descriptor set   (sampler + diffuse + specular)   <- AssetManager
//   group 2 = transform descriptor set (model + normal matrix)         <- AssetManager
//   group 3 = the push-constant block, plus any per-effect texture     <- Pipeline.ts
// Upstream fuses 1 and 2 into one "object" set and keeps push constants outside the sets entirely;
// see AssetManager.ts and Pipeline.ts for why each has to split here.

import { Light } from "../../lighting/lights/Light";
import { LogicalDevice } from "../../logicalDevice/LogicalDevice";
import { GraphicsPipelineOptions } from "../GraphicsPipeline";
import * as gps from "../implementations/common/GraphicsPipelineStates";
import { pushConstantSizes } from "../../renderingManager/renderer3D/Renderer3DPushConstants";

// The three layouts every renderObject pipeline binds. Upstream passes these as separate
// vk::DescriptorSetLayout parameters; there are three of them here, so they travel as one struct.
export interface RenderObjectLayouts {
  lighting: GPUBindGroupLayout;
  object: GPUBindGroupLayout;
  transform: GPUBindGroupLayout;
}

// A function rather than a constant: `GPUShaderStage` is a browser global, and evaluating it at
// module scope throws during Next's server-side render of the page that imports this file.
function vertexAndFragment(): GPUShaderStageFlags {
  return GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT;
}

function baseLayouts(layouts: RenderObjectLayouts): GPUBindGroupLayout[] {
  return [layouts.lighting, layouts.object, layouts.transform];
}

export function createTexturedPlanePipelineOptions(
  logicalDevice: LogicalDevice,
  layouts: RenderObjectLayouts,
): GraphicsPipelineOptions {
  return {
    label: "vke.TexturedPlanePipeline",
    shaders: {
      shader: "/shaders/variants/texturedPlane.wgsl",
    },
    states: {
      colorBlendState: gps.colorBlendState,
      depthStencilState: gps.depthStencilState,
      inputAssemblyState: gps.inputAssemblyStateTriangleList,
      multisampleState: gps.getMultisampleState(logicalDevice),
      rasterizationState: gps.rasterizationStateNoCull,
      vertexInputState: gps.vertexInputStateVertex,
    },
    bindGroupLayouts: baseLayouts(layouts),
  };
}

export function createObjectHighlightPipelineOptions(
  logicalDevice: LogicalDevice,
  layouts: RenderObjectLayouts,
): GraphicsPipelineOptions {
  return {
    label: "vke.ObjectHighlightPipeline",
    shaders: {
      shader: "/shaders/objectHighlight.wgsl",
    },
    states: {
      colorBlendState: gps.colorBlendStateDots,
      depthStencilState: gps.depthStencilState,
      inputAssemblyState: gps.inputAssemblyStateTriangleList,
      multisampleState: gps.getMultisampleState(logicalDevice),
      rasterizationState: gps.rasterizationStateCullBack,
      vertexInputState: gps.vertexInputStateVertexPositionOnly,
    },
    // objectHighlight.wgsl reads only the camera at group 0 and the transform at group 1, so the
    // lighting set is bound but mostly unused — a shader may declare a subset of its layout.
    bindGroupLayouts: [layouts.lighting, layouts.transform],
  };
}

export function createMagnifyWhirlMosaicPipelineOptions(
  logicalDevice: LogicalDevice,
  layouts: RenderObjectLayouts,
): GraphicsPipelineOptions {
  return {
    label: "vke.MagnifyWhirlMosaicPipeline",
    shaders: {
      shader: "/shaders/variants/magnifyWhirlMosaic.wgsl",
    },
    states: {
      colorBlendState: gps.colorBlendState,
      depthStencilState: gps.depthStencilState,
      inputAssemblyState: gps.inputAssemblyStateTriangleList,
      multisampleState: gps.getMultisampleState(logicalDevice),
      rasterizationState: gps.rasterizationStateNoCull,
      vertexInputState: gps.vertexInputStateVertex,
    },
    pushConstantRanges: [
      { stages: GPUShaderStage.FRAGMENT, size: pushConstantSizes.magnifyWhirlMosaic },
    ],
    bindGroupLayouts: baseLayouts(layouts),
  };
}

export function createMousePickingPipelineOptions(
  layouts: RenderObjectLayouts,
  pickDescriptorSetLayout: GPUBindGroupLayout,
): GraphicsPipelineOptions {
  return {
    label: "vke.MousePickingPipeline",
    shaders: {
      shader: "/shaders/mousePicking.wgsl",
    },
    states: {
      colorBlendState: gps.colorBlendState,
      depthStencilState: gps.depthStencilState,
      inputAssemblyState: gps.inputAssemblyStateTriangleList,
      multisampleState: gps.multisampleStateNone,
      rasterizationState: gps.rasterizationStateCullBack,
      vertexInputState: gps.vertexInputStateVertexPositionOnly,
    },
    // Upstream pushes the object ID per draw. That is genuinely per-draw data, so it stays a real
    // dynamic-offset uniform ring (owned by MousePicker) rather than the static push-constant
    // buffer Pipeline.ts provides.
    bindGroupLayouts: [layouts.lighting, layouts.transform, pickDescriptorSetLayout],
    colorFormat: "rgba8uint",
  };
}

export function createShadowMapPipelineOptions(
  layouts: RenderObjectLayouts,
  faceDescriptorSetLayout: GPUBindGroupLayout,
): GraphicsPipelineOptions {
  return {
    label: "vke.ShadowPipeline",
    shaders: {
      shader: "/shaders/shadow.wgsl",
    },
    states: {
      colorBlendState: gps.colorBlendStateShadow,
      depthStencilState: gps.depthStencilState,
      inputAssemblyState: gps.inputAssemblyStateTriangleList,
      multisampleState: gps.multisampleStateNone,
      rasterizationState: gps.rasterizationStateCullBack,
      vertexInputState: gps.vertexInputStateVertexPositionOnly,
    },
    // Face view-projection (dynamic offset, one slot per light/face) then the object transform.
    bindGroupLayouts: [faceDescriptorSetLayout, layouts.transform],
    depthFormat: Light.shadowMapFormat,
  };
}

export function createEllipticalDotsPipelineOptions(
  logicalDevice: LogicalDevice,
  layouts: RenderObjectLayouts,
): GraphicsPipelineOptions {
  return {
    label: "vke.EllipticalDotsPipeline",
    shaders: {
      shader: "/shaders/variants/ellipticalDots.wgsl",
    },
    states: {
      colorBlendState: gps.colorBlendState,
      depthStencilState: gps.depthStencilState,
      inputAssemblyState: gps.inputAssemblyStateTriangleList,
      multisampleState: gps.getMultisampleState(logicalDevice),
      rasterizationState: gps.rasterizationStateCullBack,
      vertexInputState: gps.vertexInputStateVertex,
    },
    pushConstantRanges: [
      { stages: GPUShaderStage.FRAGMENT, size: pushConstantSizes.ellipticalDots },
    ],
    bindGroupLayouts: baseLayouts(layouts),
  };
}

export function createCurtainPipelineOptions(
  logicalDevice: LogicalDevice,
  layouts: RenderObjectLayouts,
): GraphicsPipelineOptions {
  return {
    label: "vke.CurtainPipeline",
    shaders: {
      shader: "/shaders/variants/curtain.wgsl",
    },
    states: {
      colorBlendState: gps.colorBlendState,
      depthStencilState: gps.depthStencilState,
      inputAssemblyState: gps.inputAssemblyStateTriangleList,
      multisampleState: gps.getMultisampleState(logicalDevice),
      rasterizationState: gps.rasterizationStateNoCull,
      vertexInputState: gps.vertexInputStateVertex,
    },
    pushConstantRanges: [{ stages: vertexAndFragment(), size: pushConstantSizes.curtain }],
    bindGroupLayouts: baseLayouts(layouts),
  };
}

export function createObjectsPipelineOptions(
  logicalDevice: LogicalDevice,
  layouts: RenderObjectLayouts,
): GraphicsPipelineOptions {
  return {
    label: "vke.ObjectPipeline",
    shaders: {
      shader: "/shaders/standard.wgsl",
    },
    states: {
      colorBlendState: gps.colorBlendState,
      depthStencilState: gps.depthStencilState,
      inputAssemblyState: gps.inputAssemblyStateTriangleList,
      multisampleState: gps.getMultisampleState(logicalDevice),
      rasterizationState: gps.rasterizationStateCullBack,
      vertexInputState: gps.vertexInputStateVertex,
    },
    bindGroupLayouts: baseLayouts(layouts),
  };
}

export function createNoisyEllipticalDotsPipelineOptions(
  logicalDevice: LogicalDevice,
  layouts: RenderObjectLayouts,
  noiseView: GPUTextureView,
  noiseSampler: GPUSampler,
): GraphicsPipelineOptions {
  return {
    label: "vke.NoisyEllipticalDotsPipeline",
    shaders: {
      shader: "/shaders/variants/noisyEllipticalDots.wgsl",
    },
    states: {
      colorBlendState: gps.colorBlendState,
      depthStencilState: gps.depthStencilState,
      inputAssemblyState: gps.inputAssemblyStateTriangleList,
      multisampleState: gps.getMultisampleState(logicalDevice),
      rasterizationState: gps.rasterizationStateCullBack,
      vertexInputState: gps.vertexInputStateVertex,
    },
    pushConstantRanges: [
      { stages: GPUShaderStage.FRAGMENT, size: pushConstantSizes.noisyEllipticalDots },
    ],
    // Upstream's separate noiseDescriptorSetLayout — folded into the push-constant group.
    pushConstantGroupBindings: {
      layoutEntries: [
        { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: { viewDimension: "3d" } },
        { binding: 2, visibility: GPUShaderStage.FRAGMENT, sampler: {} },
      ],
      entries: [
        { binding: 1, resource: noiseView },
        { binding: 2, resource: noiseSampler },
      ],
    },
    bindGroupLayouts: baseLayouts(layouts),
  };
}

export function createBumpyCurtainPipelineOptions(
  logicalDevice: LogicalDevice,
  layouts: RenderObjectLayouts,
  noiseView: GPUTextureView,
  noiseSampler: GPUSampler,
): GraphicsPipelineOptions {
  return {
    label: "vke.BumpyCurtainPipeline",
    shaders: {
      shader: "/shaders/variants/bumpyCurtain.wgsl",
    },
    states: {
      colorBlendState: gps.colorBlendState,
      depthStencilState: gps.depthStencilState,
      inputAssemblyState: gps.inputAssemblyStateTriangleList,
      multisampleState: gps.getMultisampleState(logicalDevice),
      rasterizationState: gps.rasterizationStateNoCull,
      vertexInputState: gps.vertexInputStateVertex,
    },
    pushConstantRanges: [{ stages: vertexAndFragment(), size: pushConstantSizes.bumpyCurtain }],
    pushConstantGroupBindings: {
      layoutEntries: [
        { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: { viewDimension: "3d" } },
        { binding: 2, visibility: GPUShaderStage.FRAGMENT, sampler: {} },
      ],
      entries: [
        { binding: 1, resource: noiseView },
        { binding: 2, resource: noiseSampler },
      ],
    },
    bindGroupLayouts: baseLayouts(layouts),
  };
}

export function createCubeMapPipelineOptions(
  logicalDevice: LogicalDevice,
  layouts: RenderObjectLayouts,
  noiseView: GPUTextureView,
  noiseSampler: GPUSampler,
  cubeView: GPUTextureView,
  cubeSampler: GPUSampler,
): GraphicsPipelineOptions {
  return {
    label: "vke.CubeMapPipeline",
    shaders: {
      shader: "/shaders/variants/cubeMap.wgsl",
    },
    states: {
      colorBlendState: gps.colorBlendState,
      depthStencilState: gps.depthStencilState,
      inputAssemblyState: gps.inputAssemblyStateTriangleList,
      multisampleState: gps.getMultisampleState(logicalDevice),
      rasterizationState: gps.rasterizationStateCullBack,
      vertexInputState: gps.vertexInputStateVertex,
    },
    pushConstantRanges: [{ stages: GPUShaderStage.FRAGMENT, size: pushConstantSizes.cubeMap }],
    // Upstream's cubeMapDescriptorSetLayout, folded into the push-constant group.
    pushConstantGroupBindings: {
      layoutEntries: [
        { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: { viewDimension: "3d" } },
        { binding: 2, visibility: GPUShaderStage.FRAGMENT, sampler: {} },
        { binding: 3, visibility: GPUShaderStage.FRAGMENT, texture: { viewDimension: "cube" } },
        { binding: 4, visibility: GPUShaderStage.FRAGMENT, sampler: {} },
      ],
      entries: [
        { binding: 1, resource: noiseView },
        { binding: 2, resource: noiseSampler },
        { binding: 3, resource: cubeView },
        { binding: 4, resource: cubeSampler },
      ],
    },
    bindGroupLayouts: baseLayouts(layouts),
  };
}
