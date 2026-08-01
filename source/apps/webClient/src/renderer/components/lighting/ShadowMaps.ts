// Shadow-map storage for every light, owned by LightingManager.
//
// Upstream this lives inside each Light (Light::m_shadowMapDepthImageResource) with the per-face
// matrices in PointLightUniform, and LightingManager::renderPointLightShadowMaps drives the six
// passes. WebGPU shaders read the whole light set through one binding, so the per-light cubes are
// consolidated into a single depth cube-array here, plus one dynamic-offset uniform holding every
// face's view-projection. Everything else — 1024^2 faces, near 0.1 / far 100, the linear radial
// depth written by the shadow shader, the LessEqual comparison sampler — matches upstream.

import { Mat4, Vec3, mat4LookAt, mat4Multiply, mat4Perspective } from "../../utilities/Math";
import { Light } from "./lights/Light";

// Six cubes covers every ported test (the largest uses five lights).
export const MAX_SHADOW_LIGHTS = 6;
export const SHADOW_NEAR = 0.1;
export const SHADOW_FAR = 100;

// Cube-face orientations in WebGPU cube-layer order (+X,-X,+Y,-Y,+Z,-Z) with the standard cubemap
// up vectors, matching how the upstream ShadowCubeMap pass orients its faces.
const FACE_DIRS: { dir: Vec3; up: Vec3 }[] = [
  { dir: [1, 0, 0], up: [0, -1, 0] },
  { dir: [-1, 0, 0], up: [0, -1, 0] },
  { dir: [0, 1, 0], up: [0, 0, 1] },
  { dir: [0, -1, 0], up: [0, 0, -1] },
  { dir: [0, 0, 1], up: [0, -1, 0] },
  { dir: [0, 0, -1], up: [0, -1, 0] },
];

const FACE_STRIDE = 256; // minimum dynamic-offset alignment

export class ShadowMaps {
  readonly cubeArrayView: GPUTextureView;
  readonly comparisonSampler: GPUSampler;
  readonly faceUniformBuffer: GPUBuffer;

  private readonly texture: GPUTexture;
  private readonly faceViews: GPUTextureView[] = []; // light * 6 + face
  private readonly faceData = new Float32Array((MAX_SHADOW_LIGHTS * 6 * FACE_STRIDE) / 4);
  private readonly projection: Mat4;

  constructor(private readonly device: GPUDevice) {
    const { width, height } = Light.shadowMapExtent;

    this.texture = device.createTexture({
      label: "vke.ShadowCubeArray",
      size: { width, height, depthOrArrayLayers: MAX_SHADOW_LIGHTS * 6 },
      format: Light.shadowMapFormat,
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING,
    });

    this.cubeArrayView = this.texture.createView({
      label: "vke.ShadowCubeArrayView",
      dimension: "cube-array",
    });

    for (let layer = 0; layer < MAX_SHADOW_LIGHTS * 6; layer++) {
      this.faceViews.push(
        this.texture.createView({
          label: `vke.ShadowFace${layer}`,
          dimension: "2d",
          baseArrayLayer: layer,
          arrayLayerCount: 1,
        }),
      );
    }

    // Matches LightingManager::createShadowMapSampler: linear PCF, LessOrEqual compare.
    this.comparisonSampler = device.createSampler({
      label: "vke.ShadowSampler",
      compare: "less-equal",
      magFilter: "linear",
      minFilter: "linear",
    });

    this.faceUniformBuffer = device.createBuffer({
      label: "vke.ShadowFaceViewProjections",
      size: MAX_SHADOW_LIGHTS * 6 * FACE_STRIDE,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });

    this.projection = mat4Perspective(Math.PI / 2, 1, SHADOW_NEAR, SHADOW_FAR);
    // Same flip as PointLight.cpp's `projection[1][1] *= -1`: the GL-style cube-face lookAt
    // matrices assume clip +y lands on the BOTTOM texture row, but WebGPU puts clip +y on the top
    // row. Without this every face is vertically mirrored, so shadows land in the wrong places and
    // the face seams show up as boxes.
    this.projection[5] *= -1;
  }

  faceView(lightIndex: number, face: number): GPUTextureView {
    return this.faceViews[lightIndex * 6 + face];
  }

  faceUniformOffset(lightIndex: number, face: number): number {
    return (lightIndex * 6 + face) * FACE_STRIDE;
  }

  // Recomputes and uploads every face's view-projection. Each slot is viewProj (64B) then
  // lightPos.xyz + far (16B), which is what the shadow shader's linear-depth write reads.
  updateFaceMatrices(lightPositions: Vec3[]): void {
    lightPositions.slice(0, MAX_SHADOW_LIGHTS).forEach((pos, i) => {
      FACE_DIRS.forEach(({ dir, up }, f) => {
        const target: Vec3 = [pos[0] + dir[0], pos[1] + dir[1], pos[2] + dir[2]];
        const viewProjection = mat4Multiply(this.projection, mat4LookAt(pos, target, up));
        const base = ((i * 6 + f) * FACE_STRIDE) / 4;
        this.faceData.set(viewProjection, base);
        this.faceData.set([pos[0], pos[1], pos[2], SHADOW_FAR], base + 16);
      });
    });

    this.device.queue.writeBuffer(this.faceUniformBuffer, 0, this.faceData);
  }
}
