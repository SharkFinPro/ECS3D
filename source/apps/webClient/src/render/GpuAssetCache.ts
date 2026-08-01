// Port of source/libs/render/GpuAssetCache.{h,cpp} - the uuid -> GPU object cache.
//
// DEVIATION: upstream's loadModel/loadTexture are synchronous, so getRenderObject can build and
// return a RenderObject in one call. In the browser they are Promises, so this splits into a
// non-blocking lookup: getRenderObject kicks off the loads, records the pending work, and returns
// null until every dependency has resolved. The render system already skips a null, so an object
// simply pops in a frame or two after the snapshot rather than stalling the frame.
//
// A failed load is cached as a permanent miss - retrying a 404 every frame would hammer the server.

import type { AssetRegistry } from "../data/assets/AssetRegistry";
import type { WebGPUEngine } from "../renderer/WebGPUEngine";
import type { Model } from "../renderer/components/assets/objects/Model";
import type { RenderObject } from "../renderer/components/assets/objects/RenderObject";
import type { Texture2D } from "../renderer/components/assets/textures/Texture2D";

interface CachedRenderObject {
  renderObject: RenderObject;
  modelUUID: string;
  textureUUID: string;
  specularMapUUID: string;
}

interface CachedGizmo {
  renderObject: RenderObject;
  path: string;
}

export class GpuAssetCache {
  private readonly models = new Map<string, Model | null>();
  private readonly textures = new Map<string, Texture2D | null>();
  private readonly pending = new Set<string>();

  private readonly renderObjects = new Map<string, CachedRenderObject>();
  private readonly colliderGizmos = new Map<string, CachedGizmo>();

  constructor(
    private readonly renderer: WebGPUEngine,
    private readonly assetRegistry: AssetRegistry,
  ) {}

  getRenderer(): WebGPUEngine {
    return this.renderer;
  }

  // Null while the mesh/textures are still loading, or if any of them failed or is unregistered.
  getRenderObject(
    ownerUUID: string,
    modelUUID: string,
    textureUUID: string,
    specularMapUUID: string,
  ): RenderObject | null {
    const cached = this.renderObjects.get(ownerUUID);
    if (
      cached &&
      cached.modelUUID === modelUUID &&
      cached.textureUUID === textureUUID &&
      cached.specularMapUUID === specularMapUUID
    ) {
      return cached.renderObject;
    }

    const model = this.getModel(modelUUID);
    const texture = this.getTexture(textureUUID);
    const specularMap = this.getTexture(specularMapUUID);

    if (!model || !texture || !specularMap) {
      return null;
    }

    const renderObject = this.renderer
      .getAssetManager()
      .loadRenderObject(texture, specularMap, model);

    this.renderObjects.set(ownerUUID, {
      renderObject,
      modelUUID,
      textureUUID,
      specularMapUUID,
    });

    return renderObject;
  }

  // The collider debug gizmo, keyed by path rather than uuid - these are engine-owned meshes the
  // AssetRegistry never sees.
  getColliderGizmo(ownerUUID: string, modelPath: string): RenderObject | null {
    const cached = this.colliderGizmos.get(ownerUUID);
    if (cached && cached.path === modelPath) {
      return cached.renderObject;
    }

    const model = this.loadModelByPath(modelPath);
    const white = this.loadTextureByPath("assets/textures/white.png");

    if (!model || !white) {
      return null;
    }

    const renderObject = this.renderer.getAssetManager().loadRenderObject(white, white, model);

    this.colliderGizmos.set(ownerUUID, { renderObject, path: modelPath });

    return renderObject;
  }

  private getModel(uuid: string): Model | null {
    const record = this.assetRegistry.getByUUID(uuid);
    if (!record) {
      return null;
    }

    return this.loadModelByPath(record.path);
  }

  private getTexture(uuid: string): Texture2D | null {
    const record = this.assetRegistry.getByUUID(uuid);
    if (!record) {
      return null;
    }

    return this.loadTextureByPath(record.path);
  }

  private loadModelByPath(path: string): Model | null {
    const resolved = resolveAssetPath(path);

    if (this.models.has(resolved)) {
      return this.models.get(resolved) ?? null;
    }

    this.startLoad(resolved, async () => {
      const model = await this.renderer.getAssetManager().loadModel(resolved);
      this.models.set(resolved, model);
    }, () => this.models.set(resolved, null));

    return null;
  }

  private loadTextureByPath(path: string): Texture2D | null {
    const resolved = resolveAssetPath(path);

    if (this.textures.has(resolved)) {
      return this.textures.get(resolved) ?? null;
    }

    this.startLoad(resolved, async () => {
      const texture = await this.renderer.getAssetManager().loadTexture(resolved);
      this.textures.set(resolved, texture);
    }, () => this.textures.set(resolved, null));

    return null;
  }

  private startLoad(key: string, load: () => Promise<void>, onFailure: () => void): void {
    if (this.pending.has(key)) {
      return;
    }

    this.pending.add(key);

    void load()
      .catch((error) => {
        console.error(`[Client] Failed to load asset "${key}":`, error);
        onFailure();
      })
      .finally(() => this.pending.delete(key));
  }
}

// Asset paths are relative to the executable's working directory upstream ("assets/models/x.glb");
// in the browser they resolve against the site root, where public/assets mirrors the server's
// defaultAssets tree.
function resolveAssetPath(path: string): string {
  return path.startsWith("/") ? path : `/${path}`;
}
