// Port of source/libs/data/Replication.{h,cpp} - the apply half.
//
// The build/pack side is the editor's and the server's; this client only receives. That leaves four
// entry points, matching ClientApp's four replication handlers:
//
//   unpackStateDelta     per-tick motion (uuid + local transform per object)
//   applyComponentEdit   one component's packed blob, re-broadcast after an editor changed it
//   applyObjectSpawned   one packed object a script created at runtime
//   applyObjectDestroyed one uuid a script destroyed at runtime

import { MessageReader } from "../net/Protocol";
import { GameObject } from "./objects/Object";
import type { ObjectManager } from "./objects/ObjectManager";
import { ComponentType, subComponentTypeToParent } from "./objects/components/Component";
import { Script } from "./objects/components/Script";
import { Transform } from "./objects/components/Transform";

export function unpackStateDelta(objectManager: ObjectManager, payload: Uint8Array): void {
  const reader = new MessageReader(payload);

  const count = reader.readUint32();
  for (let i = 0; i < count; ++i) {
    const uuid = reader.readString();

    // Delta order is position, rotation, scale - NOT the position/scale/rotation order Transform's
    // own pack() uses.
    const position = reader.readVec3();
    const rotation = reader.readVec3();
    const scale = reader.readVec3();

    const object = objectManager.getObjectByUUID(uuid);
    if (!object) {
      continue;
    }

    const transform = object.getComponent<Transform>(ComponentType.transform);
    if (!transform) {
      continue;
    }

    // The delta carries LOCAL transforms, so they go straight back as local values; world space is
    // recomposed by walking parents at read time.
    transform.setLocalPosition(position);
    transform.setLocalRotation(rotation);
    transform.setLocalScale(scale);
  }
}

export function applyComponentEdit(objectManager: ObjectManager, payload: Uint8Array): void {
  const reader = new MessageReader(payload);

  const objectUUID = reader.readString();

  const object = objectManager.getObjectByUUID(objectUUID);
  if (!object) {
    return;
  }

  // Each component packs its type (or, for colliders, its subtype) first as a discriminator.
  const componentType = reader.readEnum() as ComponentType;

  // Scripts live in their own list keyed by class name, and pack the name next so the right one can
  // be found before unpack() reads the remaining field data.
  if (componentType === ComponentType.script) {
    const className = reader.readString();

    for (const script of object.getScripts()) {
      if (script.getClassName() === className) {
        (script as Script).unpack(reader);
        return;
      }
    }

    return;
  }

  // Colliders pack their subtype as the discriminator, but the component map is keyed by the parent
  // (collider) type, so map back before looking it up.
  const lookupType = subComponentTypeToParent.get(componentType) ?? componentType;

  const component = object.getComponents().get(lookupType);
  if (component) {
    component.unpack(reader);
  }
}

export function applyObjectSpawned(objectManager: ObjectManager, payload: Uint8Array): void {
  // Symmetric with buildObjectSpawned: reconstruct one root object exactly as ObjectManager.unpack
  // does per object - fresh object, registered so it has a manager, then unpacked from the blob.
  const reader = new MessageReader(payload);

  const object = new GameObject();
  objectManager.addObject(object);
  object.unpack(reader);
}

export function applyObjectDestroyed(objectManager: ObjectManager, payload: Uint8Array): void {
  const reader = new MessageReader(payload);

  const object = objectManager.getObjectByUUID(reader.readString());
  if (!object) {
    return;
  }

  objectManager.removeObject(object);
}
