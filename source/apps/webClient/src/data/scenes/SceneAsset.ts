// Port of source/libs/data/scenes/SceneAsset.{h,cpp}.

import type { MessageReader } from "../../net/Protocol";
import type { ComponentRegistry } from "../ComponentRegistry";
import { ObjectManager } from "../objects/ObjectManager";

export class SceneAsset {
  private readonly objectManager: ObjectManager;

  constructor(
    private readonly uuid: string,
    private readonly name: string,
    componentRegistry: ComponentRegistry,
  ) {
    this.objectManager = new ObjectManager(componentRegistry);
  }

  getUUID(): string {
    return this.uuid;
  }

  getName(): string {
    return this.name;
  }

  getObjectManager(): ObjectManager {
    return this.objectManager;
  }

  static unpack(reader: MessageReader, componentRegistry: ComponentRegistry): SceneAsset {
    const uuid = reader.readString();
    const name = reader.readString();

    const scene = new SceneAsset(uuid, name, componentRegistry);
    scene.objectManager.unpack(reader);

    return scene;
  }
}
