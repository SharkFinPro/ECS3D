// Port of source/components/pipelines/uniformBuffers/UniformBuffer.{h,cpp}.
//
// Upstream keeps one persistently-mapped buffer per frame in flight and memcpy's into the mapped
// pointer. WebGPU forbids mapping a buffer that is also GPU-usable, so the ring collapses to a
// single buffer written with queue.writeBuffer — which is the recommended path and fast enough at
// this engine's sizes (WebGPUPortGuide.md §1 UniformBuffer row, gotcha 6). The destructor's
// waitIdle goes away too: WebGPU defers destruction of resources the GPU still holds.

export class UniformBuffer {
  private constructor(
    private readonly buffer: GPUBuffer,
    readonly size: number,
  ) {}

  static create(device: GPUDevice, label: string, size: number): UniformBuffer {
    // Uniform buffers must be a multiple of 16 bytes.
    const alignedSize = Math.max(16, Math.ceil(size / 16) * 16);

    return new UniformBuffer(
      device.createBuffer({
        label,
        size: alignedSize,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
      }),
      alignedSize,
    );
  }

  // Analogue of UniformBuffer::update(currentFrame, data) — there is no frame index because there
  // is no per-frame ring.
  update(queue: GPUQueue, data: BufferSource, offset = 0): void {
    queue.writeBuffer(this.buffer, offset, data);
  }

  getBuffer(): GPUBuffer {
    return this.buffer;
  }

  destroy(): void {
    this.buffer.destroy();
  }
}
