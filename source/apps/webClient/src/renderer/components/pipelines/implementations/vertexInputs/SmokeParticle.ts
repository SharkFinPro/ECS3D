// Port of source/components/pipelines/implementations/vertexInputs/SmokeParticle.h.
//
// DEVIATION: upstream binds the particle buffer as a *vertex* buffer and draws GL_POINTS, sizing
// each sprite with gl_PointSize and shading it with gl_PointCoord. WebGPU has neither builtin, so
// particles are drawn as instanced billboard quads that vertex-pull from the same buffer bound as
// a read-only storage buffer (draw(6, numParticles)). The memory layout below is therefore the
// storage-buffer ABI rather than a GPUVertexBufferLayout — it is what SmokeSystem seeds and what
// smoke.wgsl reads on both the compute and vertex sides.

export const SMOKE_PARTICLE_STRIDE_FLOATS = 8;
export const SMOKE_PARTICLE_STRIDE_BYTES = SMOKE_PARTICLE_STRIDE_FLOATS * 4; // 32

export class SmokeParticle {
  // f32 offsets into the packed struct: vec4 positionTtl, vec4 velocityColor.
  static readonly positionOffset = 0; // xyz
  static readonly ttlOffset = 3;
  static readonly velocityOffset = 4; // xyz
  static readonly colorOffset = 7;

  static getStride(): number {
    return SMOKE_PARTICLE_STRIDE_BYTES;
  }
}
