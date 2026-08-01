// Port of source/components/assets/AssetManager.{h,cpp} — loads and owns textures, models,
// render objects, fonts and smoke systems, and owns the descriptor set layouts those assets are
// bound through.
//
// The descriptor-pool machinery upstream needs (pool array, growth, m_currentDescriptorPoolSize)
// is gone: WebGPU creates bind groups straight from the device (WebGPUPortGuide.md §1).
// Loading is async here because the browser fetches assets over HTTP rather than reading a
// preloaded virtual filesystem.
//
// DEVIATION: upstream's single objectDescriptorSetLayout covers a RenderObject's textures AND its
// transform. This port splits them into two layouts — the material textures (group 1) and the
// transform uniform (group 2) — because the shadow, mouse-picking and object-highlight pipelines
// need the transform WITHOUT the textures, and a WebGPU pipeline layout is a fixed array of bind
// group layouts. createCloud()/ray tracing layouts are absent: not ported.

import { LogicalDevice } from "../logicalDevice/LogicalDevice";
import { Font } from "./fonts/Font";
import { Model } from "./objects/Model";
import { RenderObject } from "./objects/RenderObject";
import { Texture } from "./textures/Texture";
import { Texture2D } from "./textures/Texture2D";
import { Texture3D } from "./textures/Texture3D";
import { TextureCubemap } from "./textures/TextureCubemap";

export class AssetManager {
  private readonly device: GPUDevice;
  private readonly materialSampler: GPUSampler;

  private readonly objectDescriptorSetLayout: GPUBindGroupLayout;
  private readonly transformDescriptorSetLayout: GPUBindGroupLayout;
  private readonly fontDescriptorSetLayout: GPUBindGroupLayout;
  private readonly smokeSystemDescriptorSetLayout: GPUBindGroupLayout;
  private readonly smokeTransformDescriptorSetLayout: GPUBindGroupLayout;

  private readonly textures = new Map<string, Promise<Texture2D>>();
  private readonly models = new Map<string, Promise<Model>>();
  private readonly fontPaths = new Map<string, string>();
  private readonly fonts = new Map<string, Promise<Font>>();

  private noiseTexture: Texture3D | null = null;
  private cubeMapTexture: TextureCubemap | null = null;

  constructor(private readonly logicalDevice: LogicalDevice) {
    this.device = logicalDevice.getDevice();
    this.materialSampler = Texture2D.createSampler(this.device);

    this.objectDescriptorSetLayout = this.createObjectDescriptorSetLayout();
    this.transformDescriptorSetLayout = this.createTransformDescriptorSetLayout();
    this.fontDescriptorSetLayout = this.createFontDescriptorSetLayout();
    this.smokeSystemDescriptorSetLayout = this.createSmokeSystemDescriptorSetLayout();
    this.smokeTransformDescriptorSetLayout = this.createSmokeTransformDescriptorSetLayout();

    // Engine-bundled typeface, the analogue of upstream's source/assets contents.
    this.registerFont("Roboto", "/assets/fonts/Roboto-VariableFont_wdth,wght.ttf");
  }

  // Cached by path, so a texture shared by several objects is uploaded once.
  loadTexture(path: string): Promise<Texture2D> {
    let texture = this.textures.get(path);
    if (!texture) {
      texture = Texture2D.load(this.device, path, this.materialSampler);
      this.textures.set(path, texture);
    }
    return texture;
  }

  loadModel(path: string): Promise<Model> {
    let model = this.models.get(path);
    if (!model) {
      model = Model.load(this.device, path);
      this.models.set(path, model);
    }
    return model;
  }

  loadRenderObject(texture: Texture, specularMap: Texture, model: Model): RenderObject {
    return new RenderObject(
      this.device,
      model,
      texture,
      specularMap,
      this.objectDescriptorSetLayout,
    );
  }

  registerFont(fontName: string, fontPath: string): void {
    this.fontPaths.set(fontName, fontPath);
  }

  getFont(fontName: string): Promise<Font> {
    let font = this.fonts.get(fontName);
    if (!font) {
      const path = this.fontPaths.get(fontName);
      if (!path) throw new Error(`Font "${fontName}" was never registered`);
      font = Font.load(path);
      this.fonts.set(fontName, font);
    }
    return font;
  }

  // The shared 3D noise volume. Upstream this is a plain asset load; here it is synthesized once
  // and reused by every pipeline that samples it (see Texture3D.ts).
  getNoiseTexture(): Texture3D {
    this.noiseTexture ??= Texture3D.createNoise(this.device);
    return this.noiseTexture;
  }

  async getCubeMapTexture(): Promise<TextureCubemap> {
    this.cubeMapTexture ??= await TextureCubemap.load(this.device);
    return this.cubeMapTexture;
  }

  getMaterialSampler(): GPUSampler {
    return this.materialSampler;
  }

  getLogicalDevice(): LogicalDevice {
    return this.logicalDevice;
  }

  getObjectDescriptorSetLayout(): GPUBindGroupLayout {
    return this.objectDescriptorSetLayout;
  }

  getTransformDescriptorSetLayout(): GPUBindGroupLayout {
    return this.transformDescriptorSetLayout;
  }

  getFontDescriptorSetLayout(): GPUBindGroupLayout {
    return this.fontDescriptorSetLayout;
  }

  getSmokeSystemDescriptorSetLayout(): GPUBindGroupLayout {
    return this.smokeSystemDescriptorSetLayout;
  }

  getSmokeTransformDescriptorSetLayout(): GPUBindGroupLayout {
    return this.smokeTransformDescriptorSetLayout;
  }

  // sampler + diffuse + specular, as consumed by RenderObject's descriptor set.
  private createObjectDescriptorSetLayout(): GPUBindGroupLayout {
    return this.device.createBindGroupLayout({
      label: "vke.ObjectDescriptorSetLayout",
      entries: [
        { binding: 0, visibility: GPUShaderStage.FRAGMENT, sampler: {} },
        { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: {} },
        { binding: 2, visibility: GPUShaderStage.FRAGMENT, texture: {} },
      ],
    });
  }

  // The per-object model + normal matrix uniform.
  private createTransformDescriptorSetLayout(): GPUBindGroupLayout {
    return this.device.createBindGroupLayout({
      label: "vke.TransformDescriptorSetLayout",
      entries: [{ binding: 0, visibility: GPUShaderStage.VERTEX, buffer: { type: "uniform" } }],
    });
  }

  private createFontDescriptorSetLayout(): GPUBindGroupLayout {
    return this.device.createBindGroupLayout({
      label: "vke.FontDescriptorSetLayout",
      entries: [
        { binding: 0, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } },
        { binding: 1, visibility: GPUShaderStage.FRAGMENT, sampler: { type: "filtering" } },
      ],
    });
  }

  // Compute side: deltaTime uniform, in/out particle buffers, smoke params uniform.
  private createSmokeSystemDescriptorSetLayout(): GPUBindGroupLayout {
    return this.device.createBindGroupLayout({
      label: "vke.SmokeSystemDescriptorSetLayout",
      entries: [
        { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: "uniform" } },
        { binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: { type: "read-only-storage" } },
        { binding: 2, visibility: GPUShaderStage.COMPUTE, buffer: { type: "storage" } },
        { binding: 3, visibility: GPUShaderStage.COMPUTE, buffer: { type: "uniform" } },
      ],
    });
  }

  // Render side: transform uniform + the particle buffer the vertex stage pulls billboards from.
  private createSmokeTransformDescriptorSetLayout(): GPUBindGroupLayout {
    return this.device.createBindGroupLayout({
      label: "vke.SmokeTransformDescriptorSetLayout",
      entries: [
        { binding: 0, visibility: GPUShaderStage.VERTEX, buffer: { type: "uniform" } },
        { binding: 1, visibility: GPUShaderStage.VERTEX, buffer: { type: "read-only-storage" } },
      ],
    });
  }
}
