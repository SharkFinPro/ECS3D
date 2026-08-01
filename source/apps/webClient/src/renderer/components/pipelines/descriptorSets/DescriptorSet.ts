// Port of source/components/pipelines/descriptorSets/DescriptorSet.{h,cpp}.
//
// "The closest thing to a free lunch in this port" (WebGPUPortGuide.md §1): a wgpu::BindGroup is
// an immutable bundle of buffer/texture/sampler bindings created straight from the device. The
// wrapper keeps its exact upstream role — own a layout plus the set built from it, and hand the
// layout to whichever pipeline config needs it — but the descriptor pool, the pool-growth
// bookkeeping, and the per-frame-in-flight set array all disappear (there is no pool, and the
// bindings this engine uses do not change between frames in flight).

export class DescriptorSet {
  private constructor(
    private readonly layout: GPUBindGroupLayout,
    private readonly bindGroup: GPUBindGroup,
  ) {}

  static create(
    device: GPUDevice,
    label: string,
    layoutEntries: GPUBindGroupLayoutEntry[],
    entries: (layout: GPUBindGroupLayout) => GPUBindGroupEntry[],
  ): DescriptorSet {
    const layout = device.createBindGroupLayout({ label: `${label}.Layout`, entries: layoutEntries });
    const bindGroup = device.createBindGroup({ label, layout, entries: entries(layout) });

    return new DescriptorSet(layout, bindGroup);
  }

  getDescriptorSetLayout(): GPUBindGroupLayout {
    return this.layout;
  }

  getDescriptorSet(): GPUBindGroup {
    return this.bindGroup;
  }
}
