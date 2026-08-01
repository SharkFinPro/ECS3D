// Port of source/libs/data/objects/ObjectManager.{h,cpp}.
//
// Read-only half: instantiate/duplicateObject/reassignUUIDs are not ported (those are the editor's
// and the scripting bindings' structural-edit paths, both server-side), nor is the deferred
// deleteObjectsMarkedForDeletion queue - objectDestroyed removes immediately here.
//
// Keeps upstream's two lists: `objects` is the scene roots (what pack/unpack walks) and `allObjects`
// is every node flattened (what the systems iterate).

import type { MessageReader } from "../../net/Protocol";
import type { ComponentRegistry } from "../ComponentRegistry";
import { GameObject } from "./Object";

export class ObjectManager {
  private objects: GameObject[] = [];
  private allObjects: GameObject[] = [];

  // uuid -> object. Upstream scans m_allObjects linearly, which is fine for a 16-byte uuid compare in
  // C++ but not here: unpackStateDelta looks up every entry of every delta, so a 544-object scene was
  // doing ~300k 36-char string compares per delta at 50Hz. Populated by registerUUID rather than
  // addObject, because an object's uuid is only known once it unpacks (addObject runs first).
  private readonly byUUID = new Map<string, GameObject>();

  constructor(private readonly componentRegistry: ComponentRegistry) {}

  getComponentRegistry(): ComponentRegistry {
    return this.componentRegistry;
  }

  addObject(object: GameObject): void {
    object.setManager(this);

    this.allObjects.push(object);

    const parent = object.getParent();
    if (!parent) {
      this.objects.push(object);
    } else {
      parent.addChild(object);
    }
  }

  getObjects(): readonly GameObject[] {
    return this.objects;
  }

  getAllObjects(): readonly GameObject[] {
    return this.allObjects;
  }

  getObjectByUUID(uuid: string): GameObject | null {
    return this.byUUID.get(uuid) ?? null;
  }

  // Called by GameObject the moment it learns its uuid (unpack/loadFromJSON), which is after
  // addObject has already run.
  registerUUID(object: GameObject): void {
    const uuid = object.getUUID();
    if (uuid) {
      this.byUUID.set(uuid, object);
    }
  }

  // Drops an object and its whole subtree. Upstream defers this to
  // deleteObjectsMarkedForDeletion so a removal mid-tick can't invalidate the sim's iteration; this
  // client has no tick loop to protect, and applyObjectDestroyed is the only caller.
  removeObject(object: GameObject): void {
    const parent = object.getParent();
    if (parent) {
      parent.removeChild(object);
    } else {
      this.objects = this.objects.filter((candidate) => candidate !== object);
    }

    const removed = new Set<GameObject>();
    const collect = (current: GameObject) => {
      removed.add(current);
      for (const child of current.getChildren()) {
        collect(child);
      }
    };
    collect(object);

    this.allObjects = this.allObjects.filter((candidate) => !removed.has(candidate));

    // Prune the index too, or a destroyed uuid would keep resolving to a detached object.
    for (const gone of removed) {
      this.byUUID.delete(gone.getUUID());
    }
  }

  unpack(reader: MessageReader): void {
    const objectCount = reader.readUint32();

    for (let i = 0; i < objectCount; ++i) {
      const object = new GameObject();
      this.addObject(object);

      object.unpack(reader);
    }
  }

  // The JSON counterpart, used by the prefab/scene blobs that travel inside a snapshot.
  loadObjects(objectsData: unknown): void {
    if (!Array.isArray(objectsData)) {
      return;
    }

    for (const raw of objectsData) {
      const objectData = raw as Record<string, unknown>;

      const object = new GameObject(this);
      object.loadFromJSON(objectData);
      this.addObject(object);

      if (objectData.children !== undefined) {
        object.loadChildren(objectData.children);
      }
    }
  }
}
