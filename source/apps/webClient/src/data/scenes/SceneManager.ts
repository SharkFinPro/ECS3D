// Port of source/libs/data/scenes/SceneManager.{h,cpp}.
//
// The scene store plus the play/pause/stop state. The status is replicated to the client (a
// sceneStatus message after every snapshot) but drives nothing here - the client renders whatever
// state the authoritative server streams, running or not.

import type { SceneAsset } from "./SceneAsset";

// Packed as a 4-byte int (unsized `enum class`).
export enum SceneStatus {
  running = 0,
  stopped = 1,
  paused = 2,
}

export class SceneManager {
  private readonly scenes = new Map<string, SceneAsset>();
  private currentScene: SceneAsset | null = null;
  private sceneStatus = SceneStatus.stopped;

  // Keyed by uuid and first-wins, matching upstream - which is why applying a fresh snapshot has to
  // clear() first rather than relying on addScene to replace.
  addScene(scene: SceneAsset): void {
    if (!this.scenes.has(scene.getUUID())) {
      this.scenes.set(scene.getUUID(), scene);
    }
  }

  clear(): void {
    this.scenes.clear();
    this.currentScene = null;
  }

  getScenes(): ReadonlyMap<string, SceneAsset> {
    return this.scenes;
  }

  getScene(uuid: string): SceneAsset | null {
    return this.scenes.get(uuid) ?? null;
  }

  loadScene(scene: SceneAsset): void {
    this.currentScene = scene;
  }

  getCurrentScene(): SceneAsset | null {
    return this.currentScene;
  }

  getSceneStatus(): SceneStatus {
    return this.sceneStatus;
  }

  setSceneStatus(status: SceneStatus): void {
    this.sceneStatus = status;
  }
}
