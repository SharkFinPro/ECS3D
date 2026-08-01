// Port of source/libs/data/ProjectPacker.{h,cpp} - unpack half only.
//
// pack() is not ported: this client never sends project state back. The atomic-load property is
// preserved and matters here as much as upstream - everything that can throw is parsed into local
// instances first, so a malformed snapshot leaves the currently-rendered project intact rather than
// blanking the view.

import type { MessageReader } from "../net/Protocol";
import { AssetRegistry, AssetType } from "./assets/AssetRegistry";
import type { ComponentRegistry } from "./ComponentRegistry";
import { SceneAsset } from "./scenes/SceneAsset";
import type { SceneManager } from "./scenes/SceneManager";

export class ProjectPacker {
  constructor(
    private readonly assetRegistry: AssetRegistry,
    private readonly sceneManager: SceneManager,
    private readonly componentRegistry: ComponentRegistry,
  ) {}

  unpack(reader: MessageReader): void {
    // Parse phase - nothing below touches live state.
    const parsedAssets = new AssetRegistry();
    parsedAssets.unpack(reader);

    const sceneCount = reader.readUint32();
    const parsedScenes: SceneAsset[] = [];
    for (let i = 0; i < sceneCount; ++i) {
      parsedScenes.push(SceneAsset.unpack(reader, this.componentRegistry));
    }

    const currentSceneUUID = reader.readString();

    // Commit phase - nothing below throws, so the swap is all-or-nothing. A fresh snapshot must
    // REPLACE the current state (both registries are keyed and won't overwrite), so clear first.
    this.sceneManager.clear();
    this.assetRegistry.adopt(parsedAssets);

    for (const scene of parsedScenes) {
      this.sceneManager.addScene(scene);

      // Scenes are registered as assets too, matching upstream - AssetRegistry.pack skips Scene
      // records, so this doesn't double-count on the next snapshot.
      this.assetRegistry.registerAsset({
        uuid: scene.getUUID(),
        type: AssetType.Scene,
        path: scene.getName(),
        className: "",
        body: "",
        displayName: "",
      });
    }

    if (currentSceneUUID) {
      const scene = this.sceneManager.getScene(currentSceneUUID);
      if (scene) {
        this.sceneManager.loadScene(scene);
      }
    }
  }
}
