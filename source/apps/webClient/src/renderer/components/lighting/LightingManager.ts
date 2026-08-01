// Port of source/components/lighting/LightingManager.{h,cpp}.
//
// Same role and same per-frame contract as upstream: tests create lights, call renderLight() on
// the ones they want lit this frame, and the manager uploads them and owns the lighting
// descriptor set every lit pipeline binds. clearLightsToRender() runs at the top of each frame.
//
// DEVIATIONS:
//  - Point and spot lights share ONE storage buffer instead of two uniform arrays; the light's
//    kind is encoded in its packed slot (Light.ts). WGSL reads a runtime-sized array, so a single
//    binding is both simpler and removes upstream's m_prevNumPointLights bookkeeping.
//  - The descriptor pool growth logic is gone — WebGPU has no descriptor pools.
//  - Shadow maps are consolidated into ShadowMaps.ts rather than living on each Light.
//  - The camera uniform lives here (as it does upstream, m_cameraUniform) and is exposed for the
//    few pipelines that want the camera without the lights (highlight, mouse picking).

import { Mat4, Vec3 } from "../../utilities/Math";
import { DescriptorSet } from "../pipelines/descriptorSets/DescriptorSet";
import { UniformBuffer } from "../pipelines/uniformBuffers/UniformBuffer";
import { Light, LIGHT_STRIDE_FLOATS } from "./lights/Light";
import { PointLight } from "./lights/PointLight";
import { SpotLight } from "./lights/SpotLight";
import { MAX_SHADOW_LIGHTS, SHADOW_FAR, SHADOW_NEAR, ShadowMaps } from "./ShadowMaps";

const MAX_LIGHTS = 16;

// view (64) + projection (64) + cameraPos + numLights (16) + shadow params (16).
const CAMERA_UNIFORM_SIZE = 160;

export class LightingManager {
  private readonly cameraUniform: UniformBuffer;
  private readonly lightsBuffer: GPUBuffer;
  private readonly lightingDescriptorSet: DescriptorSet;
  private readonly shadowMaps: ShadowMaps;

  private readonly lightData = new Float32Array(LIGHT_STRIDE_FLOATS * MAX_LIGHTS);
  private readonly cameraData = new Float32Array(CAMERA_UNIFORM_SIZE / 4);

  private lightsToRender: Light[] = [];

  constructor(private readonly device: GPUDevice) {
    this.shadowMaps = new ShadowMaps(device);

    this.cameraUniform = UniformBuffer.create(device, "vke.CameraUniform", CAMERA_UNIFORM_SIZE);

    this.lightsBuffer = device.createBuffer({
      label: "vke.Lights",
      size: this.lightData.byteLength,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
    });

    this.lightingDescriptorSet = this.createDescriptorSet();
  }

  createPointLight(
    position: Vec3,
    color: Vec3,
    ambient: number,
    diffuse: number,
    specular = 1.0,
  ): PointLight {
    return new PointLight({ position, color, ambient, diffuse, specular });
  }

  createSpotLight(
    position: Vec3,
    color: Vec3,
    ambient: number,
    diffuse: number,
    specular = 1.0,
  ): SpotLight {
    return new SpotLight({ position, color, ambient, diffuse, specular });
  }

  // Queues a light for this frame. Tests call this every frame, exactly as upstream.
  renderLight(light: Light): void {
    if (this.lightsToRender.length >= MAX_LIGHTS) return;
    this.lightsToRender.push(light);
  }

  clearLightsToRender(): void {
    this.lightsToRender = [];
  }

  getLightsToRender(): readonly Light[] {
    return this.lightsToRender;
  }

  getLightingDescriptorSet(): DescriptorSet {
    return this.lightingDescriptorSet;
  }

  getCameraUniform(): UniformBuffer {
    return this.cameraUniform;
  }

  getShadowMaps(): ShadowMaps {
    return this.shadowMaps;
  }

  // How many of this frame's lights actually get shadow cubes rendered.
  getShadowLightCount(shadowsEnabled: boolean): number {
    return shadowsEnabled ? Math.min(this.lightsToRender.length, MAX_SHADOW_LIGHTS) : 0;
  }

  // Analogue of LightingManager::update(currentFrame, viewPosition) — packs this frame's lights
  // and refreshes the camera uniform the lighting descriptor set exposes.
  update(
    queue: GPUQueue,
    viewMatrix: Mat4,
    projectionMatrix: Mat4,
    viewPosition: Vec3,
    shadowLightCount: number,
  ): void {
    this.lightsToRender.forEach((light, i) => {
      light.writeUniform(this.lightData, i * LIGHT_STRIDE_FLOATS);
    });
    queue.writeBuffer(
      this.lightsBuffer,
      0,
      this.lightData,
      0,
      Math.max(1, this.lightsToRender.length) * LIGHT_STRIDE_FLOATS,
    );

    this.cameraData.set(viewMatrix, 0);
    this.cameraData.set(projectionMatrix, 16);
    this.cameraData.set(viewPosition, 32);
    new Uint32Array(this.cameraData.buffer)[35] = this.lightsToRender.length;
    this.cameraData.set([SHADOW_NEAR, SHADOW_FAR, shadowLightCount, 0], 36);
    this.cameraUniform.update(queue, this.cameraData);

    this.shadowMaps.updateFaceMatrices(this.lightsToRender.map((light) => light.getPosition()));
  }

  // The set every lit pipeline binds at group 0: camera uniform, the packed light array, and the
  // shadow cube-array with its comparison sampler.
  private createDescriptorSet(): DescriptorSet {
    return DescriptorSet.create(
      this.device,
      "vke.LightingDescriptorSet",
      [
        {
          binding: 0,
          visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
          buffer: { type: "uniform" },
        },
        { binding: 1, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "read-only-storage" } },
        {
          binding: 2,
          visibility: GPUShaderStage.FRAGMENT,
          texture: { sampleType: "depth", viewDimension: "cube-array" },
        },
        { binding: 3, visibility: GPUShaderStage.FRAGMENT, sampler: { type: "comparison" } },
      ],
      () => [
        { binding: 0, resource: { buffer: this.cameraUniform.getBuffer() } },
        { binding: 1, resource: { buffer: this.lightsBuffer } },
        { binding: 2, resource: this.shadowMaps.cubeArrayView },
        { binding: 3, resource: this.shadowMaps.comparisonSampler },
      ],
    );
  }
}
