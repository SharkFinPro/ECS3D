// Port of source/libs/data/objects/components/collisions/SphereCollider.{h,cpp}.
//
// As with BoxCollider, only the data the debug gizmo needs is ported; the support function and the
// scaled-radius query belong to the server's CollisionSystem.

import type { MessageReader, Vec3 } from "../../../../net/Protocol";
import { ComponentType, vec3FromJSON } from "../Component";
import { Collider, ColliderType } from "./Collider";

export class SphereCollider extends Collider {
  private position: Vec3 = [0, 0, 0];
  private radius = 1;

  constructor() {
    super(ColliderType.sphereCollider, ComponentType.SubComponentType_sphereCollider);
  }

  getLocalPosition(): Vec3 {
    return this.position;
  }

  getLocalRadius(): number {
    return this.radius;
  }

  unpack(reader: MessageReader): void {
    this.renderCollider = reader.readBool();
    this.position = reader.readVec3();
    this.radius = reader.readFloat();
    this.isTrigger = reader.readBool();
    this.layer = reader.readUint32();
    this.mask = reader.readUint32();
  }

  loadFromJSON(data: Record<string, unknown>): void {
    this.position = vec3FromJSON(data.position, this.position);

    this.radius = Number(data.radius ?? this.radius);
    this.renderCollider = Boolean(data.renderCollider ?? this.renderCollider);

    this.isTrigger = Boolean(data.isTrigger ?? false);
    this.layer = Number(data.layer ?? 0);
    this.mask = Number(data.mask ?? 0xffffffff);
  }
}
