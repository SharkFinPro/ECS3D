// Port of source/components/assets/particleSystems/SmokeSystem.{h,cpp}.
//
// Same structure as upstream: two ping-pong particle storage buffers, a compute dispatch that
// integrates particles each frame (reading the previous frame's buffer, writing the current one),
// and a draw that renders them. The GPU->GPU ordering between the compute and the draw is free in
// WebGPU (WebGPUPortGuide.md §3.1), so all of upstream's semaphore work disappears.
//
// DEVIATIONS:
//  - Particles draw as instanced billboard quads rather than GL_POINTS (see SmokeParticle.ts).
//  - Particle counts are scaled down from upstream's 5,000,000 default: the default WebGPU
//    storage-buffer binding limit is 128 MB and billboards cost six vertices each, so the tests
//    use a 65,536 base with upstream's relative sizing between systems preserved.

import { Mat4, Vec3 } from "../../../utilities/Math";
import {
  SMOKE_PARTICLE_STRIDE_BYTES,
  SMOKE_PARTICLE_STRIDE_FLOATS,
} from "../../pipelines/implementations/vertexInputs/SmokeParticle";

const WORKGROUP_SIZE = 256;
const TTL = 8.0;

export class SmokeSystem {
  // GUI-exposed state, with SmokeSystem.cpp's defaults.
  position: Vec3;
  speed = 0.75; // m_dotSpeed
  spreadFactor = 0.3;
  maxSpreadDistance = 7.0;
  windStrength = 0.4;

  readonly numParticles: number;
  private readonly workgroups: number;

  private readonly buffers: [GPUBuffer, GPUBuffer];
  private readonly paramBuffer: GPUBuffer;
  private readonly smokeBuffer: GPUBuffer;
  private readonly transformBuffer: GPUBuffer;

  private readonly computeDescriptorSets: [GPUBindGroup, GPUBindGroup];
  private readonly renderDescriptorSets: [GPUBindGroup, GPUBindGroup];

  constructor(
    private readonly device: GPUDevice,
    computeDescriptorSetLayout: GPUBindGroupLayout,
    transformDescriptorSetLayout: GPUBindGroupLayout,
    position: Vec3,
    numParticles: number,
  ) {
    this.position = [...position] as Vec3;
    this.numParticles = numParticles;
    this.workgroups = Math.floor(numParticles / WORKGROUP_SIZE);

    const byteLength = numParticles * SMOKE_PARTICLE_STRIDE_BYTES;
    const initial = this.createShaderStorageBuffers();

    const makeBuffer = (i: number): GPUBuffer => {
      const buffer = device.createBuffer({
        label: `vke.SmokeParticles${i}`,
        size: byteLength,
        usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
      });
      device.queue.writeBuffer(buffer, 0, initial);
      return buffer;
    };
    this.buffers = [makeBuffer(0), makeBuffer(1)];

    this.paramBuffer = device.createBuffer({
      label: "vke.SmokeParam",
      size: 16, // deltaTime + padding
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });
    this.smokeBuffer = device.createBuffer({
      label: "vke.SmokeUniform",
      size: 32, // systemPosition(12) + spreadFactor + maxSpreadDistance + windStrength + padding
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });
    this.transformBuffer = device.createBuffer({
      label: "vke.SmokeTransform",
      size: 144, // view(64) + projection(64) + viewport(8) + padding(8)
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });

    // Ping-pong: frame f reads buffer[1 - f] and writes buffer[f].
    const computeSet = (inBuffer: GPUBuffer, outBuffer: GPUBuffer): GPUBindGroup =>
      device.createBindGroup({
        label: "vke.SmokeComputeDescriptorSet",
        layout: computeDescriptorSetLayout,
        entries: [
          { binding: 0, resource: { buffer: this.paramBuffer } },
          { binding: 1, resource: { buffer: inBuffer } },
          { binding: 2, resource: { buffer: outBuffer } },
          { binding: 3, resource: { buffer: this.smokeBuffer } },
        ],
      });
    this.computeDescriptorSets = [
      computeSet(this.buffers[1], this.buffers[0]),
      computeSet(this.buffers[0], this.buffers[1]),
    ];

    // The draw reads the buffer just written this frame.
    const renderSet = (buffer: GPUBuffer): GPUBindGroup =>
      device.createBindGroup({
        label: "vke.SmokeRenderDescriptorSet",
        layout: transformDescriptorSetLayout,
        entries: [
          { binding: 0, resource: { buffer: this.transformBuffer } },
          { binding: 1, resource: { buffer } },
        ],
      });
    this.renderDescriptorSets = [renderSet(this.buffers[0]), renderSet(this.buffers[1])];
  }

  // Port of SmokeSystem::createShaderStorageBuffers: particles are seeded far away
  // (x in [-1000, 1000]) with staggered negative TTLs so they spawn over time — the compute
  // shader respawns each one near systemPosition as its TTL crosses zero.
  private createShaderStorageBuffers(): Float32Array<ArrayBuffer> {
    const data = new Float32Array(this.numParticles * SMOKE_PARTICLE_STRIDE_FLOATS);
    const ttlSpan = (TTL / this.numParticles) * 1.5;
    let currentTTL = 0;

    for (let i = 0; i < this.numParticles; i++) {
      const o = i * SMOKE_PARTICLE_STRIDE_FLOATS;
      data[o + 0] = -1000 + Math.random() * 2000; // largeDistribution
      data[o + 3] = currentTTL;
      data[o + 7] = 0.25 + Math.random() * 0.75; // colorDistribution
      currentTTL -= currentTTL > -4.0 ? ttlSpan * 4.0 : ttlSpan;
    }

    return data;
  }

  // Per-frame uniform refresh. deltaTime = speed * dt, matching SmokeSystem::update.
  update(
    dtSeconds: number,
    viewMatrix: Mat4,
    projectionMatrix: Mat4,
    viewportWidth: number,
    viewportHeight: number,
  ): void {
    const queue = this.device.queue;

    queue.writeBuffer(this.paramBuffer, 0, new Float32Array([this.speed * dtSeconds, 0, 0, 0]));

    queue.writeBuffer(
      this.smokeBuffer,
      0,
      new Float32Array([
        this.position[0], this.position[1], this.position[2],
        this.spreadFactor, this.maxSpreadDistance, this.windStrength, 0, 0,
      ]),
    );

    const transform = new Float32Array(36);
    transform.set(viewMatrix, 0);
    transform.set(projectionMatrix, 16);
    transform.set([viewportWidth, viewportHeight], 32);
    queue.writeBuffer(this.transformBuffer, 0, transform);
  }

  getComputeDescriptorSet(frameIndex: number): GPUBindGroup {
    return this.computeDescriptorSets[frameIndex];
  }

  getRenderDescriptorSet(frameIndex: number): GPUBindGroup {
    return this.renderDescriptorSets[frameIndex];
  }

  getWorkgroupCount(): number {
    return this.workgroups;
  }

  destroy(): void {
    this.buffers[0].destroy();
    this.buffers[1].destroy();
    this.paramBuffer.destroy();
    this.smokeBuffer.destroy();
    this.transformBuffer.destroy();
  }
}
