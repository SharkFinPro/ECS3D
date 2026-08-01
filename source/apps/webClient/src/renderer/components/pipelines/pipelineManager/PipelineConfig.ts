// Port of source/components/pipelines/pipelineManager/PipelineConfig.h — the misc pipelines.
//
// createOffscreenToSwapchainPipelineOptions has no counterpart: this port renders the scene
// straight into the swapchain rather than into an offscreen target that an ImGui dock displays,
// so there is no blit pass (see RenderingManager.ts).

import { LogicalDevice } from "../../logicalDevice/LogicalDevice";
import { GraphicsPipelineOptions } from "../GraphicsPipeline";
import * as gps from "../implementations/common/GraphicsPipelineStates";

// GridPushConstant upstream is { mat4 viewProj; vec3 viewPosition; }. The ported grid.wgsl
// raycasts the y = 0 plane, which additionally needs the inverse view-projection — WGSL has no
// inverse(), so it is computed CPU-side and travels in the same block:
//   viewProj (64) + invViewProj (64) + viewPosition (12) + padding (4).
export const GRID_PUSH_CONSTANT_SIZE = 144;

export function createGridPipelineOptions(logicalDevice: LogicalDevice): GraphicsPipelineOptions {
  return {
    label: "vke.GridPipeline",
    shaders: {
      shader: "/shaders/grid.wgsl",
    },
    states: {
      colorBlendState: gps.colorBlendStateTransparent,
      // DEVIATION: upstream uses the depth-writing preset. The grid is a translucent overlay drawn
      // after the scene, so writing depth would occlude anything queued behind it.
      depthStencilState: gps.depthStencilStateNoWrite,
      // DEVIATION: upstream draws a triangle strip; grid.wgsl synthesizes a full-screen triangle
      // from vertex_index, so it is a 3-vertex triangle list with no vertex buffer.
      inputAssemblyState: gps.inputAssemblyStateTriangleList,
      multisampleState: gps.getMultisampleState(logicalDevice),
      rasterizationState: gps.rasterizationStateNoCull,
      vertexInputState: gps.vertexInputStateRaw,
    },
    pushConstantRanges: [
      {
        stages: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
        size: GRID_PUSH_CONSTANT_SIZE,
        // No descriptor sets, so the block lands in group 0 — matching grid.wgsl.
        group: 0,
      },
    ],
  };
}
