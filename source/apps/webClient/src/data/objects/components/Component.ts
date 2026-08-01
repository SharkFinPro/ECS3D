// Port of source/libs/data/objects/components/Component.h.
//
// Read-only half: this client renders a replicated view, so components carry unpack() (and the
// loadFromJSON that editComponent rides on) but no pack()/serialize() - nothing here is ever sent
// back. ComponentVariable's play-vs-stopped split is also absent: that is server-side simulation
// state, and unpack always writes the value the server just sent.

import type { MessageReader } from "../../../net/Protocol";
// Type-only, so this does not form a runtime import cycle with Object.ts.
import type { GameObject } from "../Object";

// The packed value is the wire discriminator, written as a 4-byte int (unsized `enum class`).
export enum ComponentType {
  transform = 0,
  modelRenderer = 1,
  rigidBody = 2,
  collider = 3,
  lightRenderer = 4,
  SubComponentType_none = 5,
  SubComponentType_boxCollider = 6,
  SubComponentType_sphereCollider = 7,
  script = 8,
  playerController = 9,
  camera = 10,
}

// Colliders pack their subtype but live under the parent "collider" key on the object.
export const subComponentTypeToParent: ReadonlyMap<ComponentType, ComponentType> = new Map([
  [ComponentType.SubComponentType_boxCollider, ComponentType.collider],
  [ComponentType.SubComponentType_sphereCollider, ComponentType.collider],
]);

// Packed ComponentType -> ComponentRegistry factory key, so unpack can build from scratch.
export const componentTypeToRegistryKey: ReadonlyMap<ComponentType, string> = new Map([
  [ComponentType.transform, "Transform"],
  [ComponentType.modelRenderer, "ModelRenderer"],
  [ComponentType.rigidBody, "RigidBody"],
  [ComponentType.lightRenderer, "LightRenderer"],
  [ComponentType.SubComponentType_boxCollider, "Box"],
  [ComponentType.SubComponentType_sphereCollider, "Sphere"],
  [ComponentType.script, "Script"],
  [ComponentType.playerController, "PlayerController"],
  [ComponentType.camera, "Camera"],
]);

export abstract class Component {
  protected owner: GameObject | null = null;

  protected constructor(
    readonly type: ComponentType,
    readonly subType: ComponentType = ComponentType.SubComponentType_none,
  ) {}

  setOwner(owner: GameObject | null): void {
    this.owner = owner;
  }

  getOwner(): GameObject | null {
    return this.owner;
  }

  // Reads this component's fields, positioned just past the type discriminator Object.unpack read.
  abstract unpack(reader: MessageReader): void;

  // The editComponent path: the server re-broadcasts an edited component as its serialize() blob.
  abstract loadFromJSON(data: Record<string, unknown>): void;
}

// Shared helper for the `[x, y, z]` arrays every serialize() emits for a glm::vec3.
export function vec3FromJSON(
  value: unknown,
  fallback: [number, number, number],
): [number, number, number] {
  if (!Array.isArray(value) || value.length < 3) {
    return fallback;
  }

  return [Number(value[0]), Number(value[1]), Number(value[2])];
}
