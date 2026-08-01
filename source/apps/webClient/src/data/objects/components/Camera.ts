// Port of source/libs/data/objects/components/Camera.{h,cpp}.
//
// fov/nearPlane/farPlane are carried and replicated but have NO visible effect, exactly as upstream:
// the projection matrix is built per-frame by the renderer at a hardcoded 45 degrees / 0.1 / 1000
// (RenderInfo.getProjectionMatrix, matching vke's own hardcoded projection). ROADMAP.md A2 tracks it.
// Only `direction` and `active` actually drive anything.

import type { MessageReader, Vec3 } from "../../../net/Protocol";
import { Component, ComponentType, vec3FromJSON } from "./Component";

export class Camera extends Component {
  private direction: Vec3 = [0, 0, -1];
  private fov = 45;
  private nearPlane = 0.1;
  private farPlane = 1000;
  private active = true;

  constructor() {
    super(ComponentType.camera);
  }

  getDirection(): Vec3 {
    return this.direction;
  }

  getFov(): number {
    return this.fov;
  }

  getNearPlane(): number {
    return this.nearPlane;
  }

  getFarPlane(): number {
    return this.farPlane;
  }

  isActive(): boolean {
    return this.active;
  }

  unpack(reader: MessageReader): void {
    this.direction = reader.readVec3();
    this.fov = reader.readFloat();
    this.nearPlane = reader.readFloat();
    this.farPlane = reader.readFloat();
    this.active = reader.readBool();
  }

  loadFromJSON(data: Record<string, unknown>): void {
    this.direction = vec3FromJSON(data.direction, this.direction);

    this.fov = Number(data.fov ?? 45);
    this.nearPlane = Number(data.nearPlane ?? 0.1);
    this.farPlane = Number(data.farPlane ?? 1000);
    this.active = data.active === undefined ? true : Boolean(data.active);
  }
}
