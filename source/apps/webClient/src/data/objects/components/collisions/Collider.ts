// Port of source/libs/data/objects/components/collisions/Collider.{h,cpp}.
//
// The client runs no CollisionSystem, so the shape data exists for one reason: the debug gizmo the
// render system draws when renderCollider is set. isTrigger/layer/mask are replicated and inert.

import { Component, ComponentType } from "../Component";

export enum ColliderType {
  boxCollider = 0,
  sphereCollider = 1,
}

export abstract class Collider extends Component {
  protected isTrigger = false;
  protected layer = 0;
  protected mask = 0xffffffff;
  protected renderCollider = false;

  protected constructor(
    readonly colliderType: ColliderType,
    subType: ComponentType,
  ) {
    super(ComponentType.collider, subType);
  }

  getColliderType(): ColliderType {
    return this.colliderType;
  }

  getRenderCollider(): boolean {
    return this.renderCollider;
  }

  getIsTrigger(): boolean {
    return this.isTrigger;
  }

  getLayer(): number {
    return this.layer;
  }

  getMask(): number {
    return this.mask;
  }
}
