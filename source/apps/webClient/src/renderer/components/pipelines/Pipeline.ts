// Port of source/components/pipelines/Pipeline.h — the shared base holding the pipeline layout,
// the pipeline handle, and the push-constant helper every concrete pipeline inherits.
//
// THE ONE REAL DIFFERENCE: WebGPU has no push constants (WebGPUPortGuide.md §1). The upstream
// `vkCmdPushConstants` call becomes a small uniform buffer, owned here, bound in its own bind
// group. Renderer3D sets each pipeline's push-constant block at most once per frame (see the
// m_pushConstants map in Renderer3D.h), so a single buffer per pipeline is exactly equivalent to
// the upstream per-draw push — no dynamic-offset ring is needed at this call rate.
//
// `pushConstants()` therefore takes no stage flags: the visibility is baked into the bind group
// layout at creation time from GraphicsPipelineOptions.pushConstantRanges.

export interface PushConstantRange {
  stages: GPUShaderStageFlags;
  offset?: number;
  size: number;
  // Bind group index the block occupies. Defaults to the slot right after the declared
  // bindGroupLayouts, which is where every ported shader puts it.
  group?: number;
}

// Extra bindings that share the push-constant bind group. Upstream a pipeline's per-effect
// textures live in their own descriptor set alongside a separate push-constant range; since the
// push-constant block is already a bind group here, the two merge into one group with the block at
// binding 0 (which is how the ported .wgsl files declare it — see e.g. bumpyCurtain.wgsl's
// group(3): params, noiseTexture, noiseSampler).
export interface PushConstantGroupBindings {
  layoutEntries: GPUBindGroupLayoutEntry[];
  entries: GPUBindGroupEntry[];
}

export abstract class Pipeline {
  protected pipelineLayout!: GPUPipelineLayout;

  private pushConstantBuffer: GPUBuffer | null = null;
  private pushConstantBindGroup: GPUBindGroup | null = null;
  private pushConstantGroup = 0;

  // Writes the push-constant block. `data` is the packed struct from Renderer3DPushConstants.ts.
  pushConstants(queue: GPUQueue, data: BufferSource, offset = 0): void {
    if (!this.pushConstantBuffer) {
      throw new Error("Pipeline was created without a push constant range");
    }
    queue.writeBuffer(this.pushConstantBuffer, offset, data);
  }

  hasPushConstants(): boolean {
    return this.pushConstantBindGroup !== null;
  }

  // Binds the push-constant block for a draw — the counterpart of the implicit binding
  // vkCmdPushConstants gets for free.
  bindPushConstants(pass: GPURenderPassEncoder | GPUComputePassEncoder): void {
    if (this.pushConstantBindGroup) {
      pass.setBindGroup(this.pushConstantGroup, this.pushConstantBindGroup);
    }
  }

  // Called by subclasses before building the pipeline layout; returns the extra bind group layout
  // to append to the caller's list (or null when the pipeline declares no push constants).
  protected createPushConstantLayout(
    device: GPUDevice,
    label: string,
    ranges: PushConstantRange[] | undefined,
    defaultGroup: number,
    extra?: PushConstantGroupBindings,
  ): GPUBindGroupLayout | null {
    if (!ranges || ranges.length === 0) return null;

    // Upstream can declare several ranges over one block (different stages covering different
    // byte spans). One uniform buffer spanning the whole block with the union of the stages'
    // visibility is the equivalent, since WGSL reads the block as a single struct.
    const size = Math.max(...ranges.map((r) => (r.offset ?? 0) + r.size));
    const stages = ranges.reduce((acc, r) => acc | r.stages, 0);
    this.pushConstantGroup = ranges[0].group ?? defaultGroup;

    const layout = device.createBindGroupLayout({
      label: `${label}.PushConstantBGL`,
      entries: [
        { binding: 0, visibility: stages, buffer: { type: "uniform" } },
        ...(extra?.layoutEntries ?? []),
      ],
    });

    this.pushConstantBuffer = device.createBuffer({
      label: `${label}.PushConstants`,
      // Uniform buffers must be a multiple of 16 bytes.
      size: Math.max(16, Math.ceil(size / 16) * 16),
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });

    this.pushConstantBindGroup = device.createBindGroup({
      label: `${label}.PushConstantBindGroup`,
      layout,
      entries: [
        { binding: 0, resource: { buffer: this.pushConstantBuffer } },
        ...(extra?.entries ?? []),
      ],
    });

    return layout;
  }
}
