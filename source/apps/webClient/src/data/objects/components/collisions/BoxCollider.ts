// Port of source/libs/data/objects/components/collisions/BoxCollider.{h,cpp}.
//
// GJK/EPA support functions and the transformed-mesh cache are not ported - they exist for the
// server's CollisionSystem, which this client does not run. Only the local offset transform survives,
// because the render system draws the debug gizmo from it.
//
// Upstream's BoxCollider::serialize omits renderCollider (SphereCollider's includes it), so the JSON
// path here leaves the flag alone too. The binary path carries it on both.

import type { MessageReader, Vec3 } from "../../../../net/Protocol";
import { ComponentType, vec3FromJSON } from "../Component";
import { Collider, ColliderType } from "./Collider";

export class BoxCollider extends Collider {
  private position: Vec3 = [0, 0, 0];
  private scale: Vec3 = [1, 1, 1];
  private rotation: Vec3 = [0, 0, 0];

  constructor() {
    super(ColliderType.boxCollider, ComponentType.SubComponentType_boxCollider);
  }

  getLocalPosition(): Vec3 {
    return this.position;
  }

  getLocalScale(): Vec3 {
    return this.scale;
  }

  getLocalRotation(): Vec3 {
    return this.rotation;
  }

  unpack(reader: MessageReader): void {
    this.renderCollider = reader.readBool();
    this.position = reader.readVec3();
    this.scale = reader.readVec3();
    this.rotation = reader.readVec3();
    this.isTrigger = reader.readBool();
    this.layer = reader.readUint32();
    this.mask = reader.readUint32();
  }

  loadFromJSON(data: Record<string, unknown>): void {
    this.position = vec3FromJSON(data.position, this.position);
    this.rotation = vec3FromJSON(data.rotation, this.rotation);
    this.scale = vec3FromJSON(data.scale, this.scale);

    this.isTrigger = Boolean(data.isTrigger ?? false);
    this.layer = Number(data.layer ?? 0);
    this.mask = Number(data.mask ?? 0xffffffff);
  }
}
