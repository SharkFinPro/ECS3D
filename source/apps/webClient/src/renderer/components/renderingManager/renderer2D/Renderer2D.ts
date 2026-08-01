// Port of source/components/renderingManager/renderer2D/Renderer2D.{h,cpp} and
// source/shaders/2D/{Rect,Triangle,Ellipse,Font}.{vert,frag}.
//
// Same Processing-style immediate-mode API and the same coordinate space: pixels, origin at the
// TOP-LEFT of the render target (upstream's vertex shaders do `ndc.y = 2y/h - 1`). WebGPU's NDC is
// Y-up where Vulkan's is Y-down, so twoD.wgsl / twoDText.wgsl flip that one axis
// (`ndc.y = 1 - 2y/h`); the pixel-space math is otherwise unchanged.
//
// DEVIATIONS:
//  - Per-primitive data rides in an instance buffer rather than a push constant, so all queued
//    primitives of a kind draw in one instanced call (see Primitives2D.ts).
//  - Z-ordering is a painter's-order key baked into that payload rather than real depth state:
//    the overlay neither tests nor writes scene depth (see PipelineConfig2D.ts).

import { Font } from "../../assets/fonts/Font";
import { LogicalDevice } from "../../logicalDevice/LogicalDevice";
import { PipelineType } from "../../pipelines/implementations/common/PipelineTypes";
import { PipelineManager } from "../../pipelines/pipelineManager/PipelineManager";
import { RenderInfo } from "../../pipelines/GraphicsPipeline";
import { GLYPH_FLOATS, RECT_FLOATS, TRIANGLE_FLOATS } from "./Primitives2D";

// --- Minimal 2D affine matrix (column-vector convention: p' = M * p) ---------------------------

interface Mat2D {
  a: number;
  b: number;
  c: number;
  d: number;
  tx: number;
  ty: number;
}

function matIdentity(): Mat2D {
  return { a: 1, b: 0, c: 0, d: 1, tx: 0, ty: 0 };
}

// Returns lhs * rhs (rhs applied first, matching glm's `current *= op`).
function matMultiply(lhs: Mat2D, rhs: Mat2D): Mat2D {
  return {
    a: lhs.a * rhs.a + lhs.c * rhs.b,
    b: lhs.b * rhs.a + lhs.d * rhs.b,
    c: lhs.a * rhs.c + lhs.c * rhs.d,
    d: lhs.b * rhs.c + lhs.d * rhs.d,
    tx: lhs.a * rhs.tx + lhs.c * rhs.ty + lhs.tx,
    ty: lhs.b * rhs.tx + lhs.d * rhs.ty + lhs.ty,
  };
}

function matTranslate(x: number, y: number): Mat2D {
  return { a: 1, b: 0, c: 0, d: 1, tx: x, ty: y };
}

function matRotateDegrees(degrees: number): Mat2D {
  const radians = (degrees * Math.PI) / 180;
  const c = Math.cos(radians);
  const s = Math.sin(radians);
  return { a: c, b: s, c: -s, d: c, tx: 0, ty: 0 };
}

// --- Queued-primitive records -----------------------------------------------------------------

interface Fill {
  r: number;
  g: number;
  b: number;
  a: number;
}

interface QueuedRect {
  mat: Mat2D;
  x: number;
  y: number;
  w: number;
  h: number;
  color: Fill;
  z: number;
}

interface QueuedTriangle {
  mat: Mat2D;
  x1: number;
  y1: number;
  x2: number;
  y2: number;
  x3: number;
  y3: number;
  color: Fill;
  z: number;
}

interface QueuedGlyph {
  mat: Mat2D;
  x: number;
  y: number;
  w: number;
  h: number;
  u0: number;
  v0: number;
  u1: number;
  v1: number;
  color: Fill;
  z: number;
}

// Instance buffers grow to fit the busiest frame and are then reused.
class GrowableBuffer {
  private buffer: GPUBuffer | null = null;
  private capacityBytes = 0;

  constructor(
    private readonly device: GPUDevice,
    private readonly label: string,
    private readonly usage: GPUBufferUsageFlags,
  ) {}

  upload(data: Float32Array<ArrayBuffer>): GPUBuffer {
    const sizeBytes = Math.max(data.byteLength, 4);
    if (!this.buffer || this.capacityBytes < sizeBytes) {
      this.buffer?.destroy();
      this.capacityBytes = Math.max(sizeBytes, this.capacityBytes * 2, 256);
      this.buffer = this.device.createBuffer({
        label: this.label,
        size: this.capacityBytes,
        usage: this.usage,
      });
    }
    if (data.byteLength > 0) {
      this.device.queue.writeBuffer(this.buffer, 0, data);
    }
    return this.buffer;
  }
}

export class Renderer2D {
  private readonly device: GPUDevice;

  private currentTransform: Mat2D = matIdentity();
  private transformStack: Mat2D[] = [];
  private currentFill: Fill = { r: 1, g: 1, b: 1, a: 1 };
  private currentFontSize = 12;
  private currentZ = 0;

  private rects: QueuedRect[] = [];
  private triangles: QueuedTriangle[] = [];
  private ellipses: QueuedRect[] = [];
  private glyphsBySize = new Map<number, QueuedGlyph[]>();

  private readonly rectGpuBuffer: GrowableBuffer;
  private readonly triangleGpuBuffer: GrowableBuffer;
  private readonly ellipseGpuBuffer: GrowableBuffer;
  private readonly glyphGpuBuffer: GrowableBuffer;

  private readonly screenUniformBuffer: GPUBuffer;
  private readonly screenDescriptorSetLayout: GPUBindGroupLayout;
  private readonly screenDescriptorSet: GPUBindGroup;
  private readonly glyphDescriptorSetCache = new Map<number, GPUBindGroup>();

  private font: Font | null = null;

  constructor(
    logicalDevice: LogicalDevice,
    private readonly fontDescriptorSetLayout: GPUBindGroupLayout,
  ) {
    this.device = logicalDevice.getDevice();

    this.screenDescriptorSetLayout = this.device.createBindGroupLayout({
      label: "vke.Renderer2D.ScreenDescriptorSetLayout",
      entries: [{ binding: 0, visibility: GPUShaderStage.VERTEX, buffer: { type: "uniform" } }],
    });

    this.screenUniformBuffer = this.device.createBuffer({
      label: "vke.Renderer2D.ScreenUniform",
      size: 16,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });

    this.screenDescriptorSet = this.device.createBindGroup({
      label: "vke.Renderer2D.ScreenDescriptorSet",
      layout: this.screenDescriptorSetLayout,
      entries: [{ binding: 0, resource: { buffer: this.screenUniformBuffer } }],
    });

    const vertexUsage = GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST;
    this.rectGpuBuffer = new GrowableBuffer(this.device, "vke.Renderer2D.RectInstances", vertexUsage);
    this.triangleGpuBuffer = new GrowableBuffer(this.device, "vke.Renderer2D.TriangleInstances", vertexUsage);
    this.ellipseGpuBuffer = new GrowableBuffer(this.device, "vke.Renderer2D.EllipseInstances", vertexUsage);
    this.glyphGpuBuffer = new GrowableBuffer(this.device, "vke.Renderer2D.GlyphInstances", vertexUsage);
  }

  // PipelineManager reads this when building the four 2D pipelines.
  getScreenDescriptorSetLayout(): GPUBindGroupLayout {
    return this.screenDescriptorSetLayout;
  }

  setFont(font: Font): void {
    this.font = font;
  }

  // --- Style / matrix stack (Renderer2D::fill/rotate/translate/push/popMatrix) -----------------

  fill(r: number, g: number, b: number, a = 255): void {
    this.currentFill = { r: r / 255, g: g / 255, b: b / 255, a: a / 255 };
  }

  pushMatrix(): void {
    this.transformStack.push(this.currentTransform);
  }

  popMatrix(): void {
    const previous = this.transformStack.pop();
    if (previous) this.currentTransform = previous;
  }

  resetMatrix(): void {
    this.currentTransform = matIdentity();
  }

  translate(x: number, y: number): void {
    this.currentTransform = matMultiply(this.currentTransform, matTranslate(x, y));
  }

  // Degrees, matching upstream's `rotate(45.0f)` (glm::radians internally).
  rotate(degrees: number): void {
    this.currentTransform = matMultiply(this.currentTransform, matRotateDegrees(degrees));
  }

  // --- Primitive queueing (Renderer2D::rect/triangle/ellipse/text) ------------------------------

  rect(x: number, y: number, width: number, height: number): void {
    this.rects.push({
      mat: this.currentTransform,
      x,
      y,
      w: width,
      h: height,
      color: this.currentFill,
      z: this.currentZ,
    });
    this.currentZ++;
  }

  triangle(x1: number, y1: number, x2: number, y2: number, x3: number, y3: number): void {
    this.triangles.push({
      mat: this.currentTransform,
      x1, y1, x2, y2, x3, y3,
      color: this.currentFill,
      z: this.currentZ,
    });
    this.currentZ++;
  }

  // (cx, cy) is the centre and width/height are full extents — Primitives2D::Ellipse's convention.
  ellipse(cx: number, cy: number, width: number, height: number): void {
    this.ellipses.push({
      mat: this.currentTransform,
      x: cx,
      y: cy,
      w: width,
      h: height,
      color: this.currentFill,
      z: this.currentZ,
    });
    this.currentZ++;
  }

  textSize(px: number): void {
    this.currentFontSize = Math.max(1, Math.round(px));
  }

  text(str: string, x: number, y: number): void {
    if (!this.font) return;

    const atlas = this.font.getAtlas(this.device, this.currentFontSize);
    let bucket = this.glyphsBySize.get(this.currentFontSize);
    if (!bucket) {
      bucket = [];
      this.glyphsBySize.set(this.currentFontSize, bucket);
    }

    let currentX = x;
    for (const ch of str) {
      const glyph = atlas.glyphs.get(ch);
      if (!glyph) continue;

      bucket.push({
        mat: this.currentTransform,
        x: currentX + glyph.bearingX,
        y: y - glyph.bearingY + atlas.maxGlyphHeight,
        w: glyph.width,
        h: glyph.height,
        u0: glyph.u0,
        v0: glyph.v0,
        u1: glyph.u1,
        v1: glyph.v1,
        color: this.currentFill,
        z: this.currentZ,
      });

      currentX += glyph.advance;
    }

    this.currentZ++;
  }

  // --- Draw + clear the queue (Renderer2D::render) ----------------------------------------------

  render(renderInfo: RenderInfo, pipelineManager: PipelineManager): void {
    const { pass, extent } = renderInfo;
    const total = this.currentZ;
    const normalizeZ = (z: number): number => (total > 0 ? 1 - z / total : 0);

    this.device.queue.writeBuffer(
      this.screenUniformBuffer,
      0,
      new Float32Array([extent.width, extent.height, 0, 0]),
    );

    if (this.rects.length > 0) {
      const data = new Float32Array(this.rects.length * RECT_FLOATS);
      this.rects.forEach((r, i) => this.writeRectLike(data, i * RECT_FLOATS, r, normalizeZ(r.z)));
      this.drawInstanced(pass, pipelineManager, PipelineType.rect, this.rectGpuBuffer.upload(data), 4, this.rects.length);
    }

    if (this.triangles.length > 0) {
      const data = new Float32Array(this.triangles.length * TRIANGLE_FLOATS);
      this.triangles.forEach((t, i) => this.writeTriangle(data, i * TRIANGLE_FLOATS, t, normalizeZ(t.z)));
      this.drawInstanced(pass, pipelineManager, PipelineType.triangle, this.triangleGpuBuffer.upload(data), 3, this.triangles.length);
    }

    if (this.ellipses.length > 0) {
      const data = new Float32Array(this.ellipses.length * RECT_FLOATS);
      this.ellipses.forEach((e, i) => this.writeRectLike(data, i * RECT_FLOATS, e, normalizeZ(e.z)));
      this.drawInstanced(pass, pipelineManager, PipelineType.ellipse, this.ellipseGpuBuffer.upload(data), 4, this.ellipses.length);
    }

    if (this.glyphsBySize.size > 0 && this.font) {
      const totalGlyphs = Array.from(this.glyphsBySize.values()).reduce((n, arr) => n + arr.length, 0);
      const data = new Float32Array(totalGlyphs * GLYPH_FLOATS);
      const groups: { fontSize: number; firstInstance: number; count: number }[] = [];

      let cursor = 0;
      for (const [fontSize, glyphs] of this.glyphsBySize) {
        const firstInstance = cursor;
        for (const glyph of glyphs) {
          this.writeGlyph(data, cursor * GLYPH_FLOATS, glyph, normalizeZ(glyph.z));
          cursor++;
        }
        groups.push({ fontSize, firstInstance, count: glyphs.length });
      }

      const buffer = this.glyphGpuBuffer.upload(data);
      pipelineManager.bindGraphicsPipeline(pass, PipelineType.font);
      pipelineManager.bindGraphicsPipelineDescriptorSet(pass, PipelineType.font, this.screenDescriptorSet, 0);
      pass.setVertexBuffer(0, buffer);

      for (const group of groups) {
        pipelineManager.bindGraphicsPipelineDescriptorSet(
          pass,
          PipelineType.font,
          this.getGlyphDescriptorSet(group.fontSize),
          1,
        );
        pass.draw(4, group.count, 0, group.firstInstance);
      }
    }

    this.createNewFrame();
  }

  private drawInstanced(
    pass: GPURenderPassEncoder,
    pipelineManager: PipelineManager,
    pipelineType: PipelineType,
    buffer: GPUBuffer,
    vertexCount: number,
    instanceCount: number,
  ): void {
    pipelineManager.bindGraphicsPipeline(pass, pipelineType);
    pipelineManager.bindGraphicsPipelineDescriptorSet(pass, pipelineType, this.screenDescriptorSet, 0);
    pass.setVertexBuffer(0, buffer);
    pass.draw(vertexCount, instanceCount);
  }

  private getGlyphDescriptorSet(fontSize: number): GPUBindGroup {
    let descriptorSet = this.glyphDescriptorSetCache.get(fontSize);
    if (!descriptorSet) {
      const atlas = this.font!.getAtlas(this.device, fontSize);
      descriptorSet = this.device.createBindGroup({
        label: `vke.Renderer2D.GlyphDescriptorSet${fontSize}`,
        layout: this.fontDescriptorSetLayout,
        entries: [
          { binding: 0, resource: atlas.texture.getImageView() },
          { binding: 1, resource: atlas.texture.getSampler() },
        ],
      });
      this.glyphDescriptorSetCache.set(fontSize, descriptorSet);
    }
    return descriptorSet;
  }

  private writeRectLike(data: Float32Array, offset: number, r: QueuedRect, z: number): void {
    const { mat } = r;
    data.set([mat.a, mat.b, mat.c, mat.d], offset);
    data.set([mat.tx, mat.ty, z, 0], offset + 4);
    data.set([r.x, r.y, r.w, r.h], offset + 8);
    data.set([r.color.r, r.color.g, r.color.b, r.color.a], offset + 12);
  }

  private writeTriangle(data: Float32Array, offset: number, t: QueuedTriangle, z: number): void {
    const { mat } = t;
    data.set([mat.a, mat.b, mat.c, mat.d], offset);
    data.set([mat.tx, mat.ty, z, 0], offset + 4);
    data.set([t.x1, t.y1, t.x2, t.y2], offset + 8);
    data.set([t.x3, t.y3, 0, 0], offset + 12);
    data.set([t.color.r, t.color.g, t.color.b, t.color.a], offset + 16);
  }

  private writeGlyph(data: Float32Array, offset: number, g: QueuedGlyph, z: number): void {
    const { mat } = g;
    data.set([mat.a, mat.b, mat.c, mat.d], offset);
    data.set([mat.tx, mat.ty, z, 0], offset + 4);
    data.set([g.x, g.y, g.w, g.h], offset + 8);
    data.set([g.u0, g.v0, g.u1, g.v1], offset + 12);
    data.set([g.color.r, g.color.g, g.color.b, g.color.a], offset + 16);
  }

  // Renderer2D::createNewFrame(): clear queued geometry, reset the z counter, and restore the
  // default matrix and fill for next frame's calls.
  createNewFrame(): void {
    this.rects = [];
    this.triangles = [];
    this.ellipses = [];
    this.glyphsBySize.clear();
    this.currentZ = 0;
    this.resetMatrix();
    this.fill(255, 255, 255, 255);
  }
}
