// Port of source/components/renderingManager/renderer3D/MousePicker.{h,cpp}.
//
// Same idea as upstream: render every registered object's ID into an offscreen rgba8uint target,
// read the texel under the cursor, and flag the object found there. IDs are 1-based (0 = nothing)
// and packed into the RGB bytes exactly like MousePicking.frag.
//
// THE ONE STRUCTURAL CHANGE (WebGPUPortGuide.md §3.3): upstream submits the offscreen work, waits
// on a fence, maps the image and reads the ID synchronously, mid-frame. The browser main thread
// can never block, so the flow becomes "kick the copy this frame, consume the result when it
// lands" — the hover flag is therefore one frame late, which is imperceptible for a hover
// highlight. `readbackInFlight` keeps at most one map outstanding.
//
// Per-object IDs are genuine per-draw data, so unlike other pipelines' push constants they use a
// real 256-byte-strided dynamic-offset uniform ring rather than the single buffer Pipeline.ts owns.

import { RenderObject } from "../../assets/objects/RenderObject";
import { LogicalDevice } from "../../logicalDevice/LogicalDevice";
import { PipelineType } from "../../pipelines/implementations/common/PipelineTypes";
import { PipelineManager } from "../../pipelines/pipelineManager/PipelineManager";
import { SwapChain } from "../../window/SwapChain";
import { Window } from "../../window/Window";

// A test-owned flag the picker writes into — the port's stand-in for the `bool*` upstream's
// MousePicker::renderObject() takes.
export interface PickFlag {
  value: boolean;
}

const MAX_PICK_OBJECTS = 64;
const PICK_SLOT_STRIDE = 256; // minimum uniform dynamic-offset alignment

export class MousePicker {
  private readonly device: GPUDevice;

  private pickTexture: GPUTexture | null = null;
  private depthTexture: GPUTexture | null = null;

  private readonly stagingBuffer: GPUBuffer;
  private readonly idBuffer: GPUBuffer;
  private readonly pickDescriptorSetLayout: GPUBindGroupLayout;
  private readonly idDescriptorSet: GPUBindGroup;

  private objects: { object: RenderObject; id: number }[] = [];
  private flags = new Map<number, PickFlag>();
  private readbackInFlight = false;

  private mouseX = -1; // device pixels, relative to the canvas
  private mouseY = -1;
  private mouseInside = false;
  private canPick = false;

  private readonly onPointerMove: (e: PointerEvent) => void;
  private readonly onPointerLeave: () => void;

  constructor(
    logicalDevice: LogicalDevice,
    private readonly swapChain: SwapChain,
    private readonly window: Window,
  ) {
    this.device = logicalDevice.getDevice();

    this.stagingBuffer = this.device.createBuffer({
      label: "vke.MousePickStaging",
      size: 256, // one bytesPerRow-aligned texel row
      usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
    });

    this.idBuffer = this.device.createBuffer({
      label: "vke.MousePickIDs",
      size: MAX_PICK_OBJECTS * PICK_SLOT_STRIDE,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });

    this.pickDescriptorSetLayout = this.device.createBindGroupLayout({
      label: "vke.MousePickDescriptorSetLayout",
      entries: [
        {
          binding: 0,
          visibility: GPUShaderStage.FRAGMENT,
          buffer: { type: "uniform", hasDynamicOffset: true },
        },
      ],
    });

    this.idDescriptorSet = this.device.createBindGroup({
      label: "vke.MousePickIDDescriptorSet",
      layout: this.pickDescriptorSetLayout,
      entries: [{ binding: 0, resource: { buffer: this.idBuffer, size: 4 } }],
    });

    const canvas = window.getCanvas();
    this.onPointerMove = (e: PointerEvent) => {
      const rect = canvas.getBoundingClientRect();
      const scale = this.window.getContentScale();
      this.mouseX = Math.floor((e.clientX - rect.left) * scale);
      this.mouseY = Math.floor((e.clientY - rect.top) * scale);
      this.mouseInside = true;
    };
    this.onPointerLeave = () => {
      this.mouseInside = false;
    };

    canvas.addEventListener("pointermove", this.onPointerMove);
    canvas.addEventListener("pointerleave", this.onPointerLeave);
  }

  getPickDescriptorSetLayout(): GPUBindGroupLayout {
    return this.pickDescriptorSetLayout;
  }

  // Mirrors upstream canMousePick(): true when the cursor was inside the viewport for the last
  // completed readback.
  canMousePick(): boolean {
    return this.canPick;
  }

  // Registers an object for this frame's picking pass.
  renderObject(object: RenderObject, flag: PickFlag): void {
    if (this.objects.length >= MAX_PICK_OBJECTS) return;

    const id = this.objects.length + 1;
    this.objects.push({ object, id });
    this.flags.set(id, flag);
  }

  // Encodes the picking pass plus the 1x1 copy under the cursor. Called after the scene pass;
  // returns whether a readback should be resolved once the queue is submitted.
  render(
    encoder: GPUCommandEncoder,
    pipelineManager: PipelineManager,
    lightingDescriptorSet: GPUBindGroup,
    getTransformDescriptorSet: (object: RenderObject) => GPUBindGroup,
  ): boolean {
    const { width, height } = this.swapChain.getExtent();

    this.canPick =
      this.mouseInside &&
      this.mouseX >= 0 &&
      this.mouseX < width &&
      this.mouseY >= 0 &&
      this.mouseY < height;

    if (this.objects.length === 0 || !this.canPick || this.readbackInFlight) {
      this.objects = [];
      this.flags = new Map();
      return false;
    }

    this.createImageResources(width, height);

    for (const { id } of this.objects) {
      this.device.queue.writeBuffer(
        this.idBuffer,
        (id - 1) * PICK_SLOT_STRIDE,
        new Uint32Array([id]),
      );
    }

    const pass = encoder.beginRenderPass({
      label: "vke.MousePickingPass",
      colorAttachments: [
        {
          view: this.pickTexture!.createView(),
          loadOp: "clear",
          storeOp: "store",
          clearValue: { r: 0, g: 0, b: 0, a: 0 },
        },
      ],
      depthStencilAttachment: {
        view: this.depthTexture!.createView(),
        depthLoadOp: "clear",
        depthStoreOp: "store",
        depthClearValue: 1,
      },
    });

    pipelineManager.bindGraphicsPipeline(pass, PipelineType.mousePicking);
    pipelineManager.bindGraphicsPipelineDescriptorSet(
      pass,
      PipelineType.mousePicking,
      lightingDescriptorSet,
      0,
    );

    for (const { object, id } of this.objects) {
      pipelineManager.bindGraphicsPipelineDescriptorSet(
        pass,
        PipelineType.mousePicking,
        getTransformDescriptorSet(object),
        1,
      );
      pipelineManager.bindGraphicsPipelineDescriptorSet(
        pass,
        PipelineType.mousePicking,
        this.idDescriptorSet,
        2,
        [(id - 1) * PICK_SLOT_STRIDE],
      );
      object.draw(pass);
    }

    pass.end();

    encoder.copyTextureToBuffer(
      { texture: this.pickTexture!, origin: { x: this.mouseX, y: this.mouseY } },
      { buffer: this.stagingBuffer, bytesPerRow: 256 },
      { width: 1, height: 1 },
    );

    return true;
  }

  // The async analogue of handleRenderedMousePickingImage(): resolve the cursor texel after
  // submit and flip the picked object's flag.
  handleRenderedMousePickingImage(): void {
    const flags = this.flags;
    this.objects = [];
    this.flags = new Map();

    this.readbackInFlight = true;
    this.stagingBuffer
      .mapAsync(GPUMapMode.READ)
      .then(() => {
        const pixel = new Uint8Array(this.stagingBuffer.getMappedRange(0, 4));
        const objectID = (pixel[0] << 16) | (pixel[1] << 8) | pixel[2];
        this.stagingBuffer.unmap();

        for (const flag of flags.values()) flag.value = false;
        const picked = flags.get(objectID);
        if (picked) picked.value = true;
      })
      .catch(() => {}) // device lost or disposed mid-map
      .finally(() => {
        this.readbackInFlight = false;
      });
  }

  private createImageResources(width: number, height: number): void {
    if (this.pickTexture && this.pickTexture.width === width && this.pickTexture.height === height) {
      return;
    }

    this.pickTexture?.destroy();
    this.depthTexture?.destroy();

    this.pickTexture = this.device.createTexture({
      label: "vke.MousePickTexture",
      size: { width, height },
      format: "rgba8uint",
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
    });
    this.depthTexture = this.device.createTexture({
      label: "vke.MousePickDepth",
      size: { width, height },
      format: "depth24plus",
      usage: GPUTextureUsage.RENDER_ATTACHMENT,
    });
  }

  dispose(): void {
    const canvas = this.window.getCanvas();
    canvas.removeEventListener("pointermove", this.onPointerMove);
    canvas.removeEventListener("pointerleave", this.onPointerLeave);
    this.pickTexture?.destroy();
    this.depthTexture?.destroy();
  }
}
