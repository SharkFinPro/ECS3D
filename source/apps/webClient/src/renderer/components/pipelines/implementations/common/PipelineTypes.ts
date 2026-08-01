// Port of source/components/pipelines/implementations/common/PipelineTypes.h.
//
// The central key tying render requests to pipelines: Renderer3D groups queued RenderObjects by
// PipelineType, and PipelineManager stores one GraphicsPipeline per value.
//
// DEVIATIONS from the upstream enum: `offscreenToSwapchain` is absent because this port renders
// the scene straight into the swapchain (there is no offscreen blit — see RenderingManager), and
// `pointLightShadowMap` collapses into `shadow`, since both light kinds shadow through the same
// cube-face pass here (see LightingManager).

export enum PipelineType {
  bumpyCurtain = "bumpyCurtain",
  crosses = "crosses",
  curtain = "curtain",
  cubeMap = "cubeMap",
  ellipticalDots = "ellipticalDots",
  magnifyWhirlMosaic = "magnifyWhirlMosaic",
  noisyEllipticalDots = "noisyEllipticalDots",
  object = "object",
  objectHighlight = "objectHighlight",
  texturedPlane = "texturedPlane",
  snake = "snake",

  ellipse = "ellipse",
  font = "font",
  grid = "grid",
  mousePicking = "mousePicking",
  rect = "rect",
  shadow = "shadow",
  triangle = "triangle",
}
