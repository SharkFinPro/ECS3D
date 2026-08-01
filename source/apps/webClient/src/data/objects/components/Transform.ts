// Port of source/libs/data/objects/components/Transform.{h,cpp}.
//
// The world-space getters compose with the parent the same (non-TRS) way upstream does: position and
// rotation ADD, scale MULTIPLIES. That is not a real transform hierarchy - a rotated parent does not
// orbit its children - but the server replicates LOCAL transforms and composes identically, so this
// must match exactly or every parented object lands somewhere the server didn't put it.
// (ROADMAP.md D5 tracks fixing it upstream; when it lands, this changes with it.)

import type { MessageReader, Vec3 } from "../../../net/Protocol";
import { Component, ComponentType, vec3FromJSON } from "./Component";

export class Transform extends Component {
  private position: Vec3 = [0, 0, 0];
  private scale: Vec3 = [0, 0, 0];
  private rotation: Vec3 = [0, 0, 0];

  constructor() {
    super(ComponentType.transform);
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

  setLocalPosition(position: Vec3): void {
    this.position = position;
  }

  setLocalScale(scale: Vec3): void {
    this.scale = scale;
  }

  setLocalRotation(rotation: Vec3): void {
    this.rotation = rotation;
  }

  getPosition(): Vec3 {
    const parent = this.parentTransform();
    if (!parent) {
      return this.position;
    }

    const base = parent.getPosition();
    return [base[0] + this.position[0], base[1] + this.position[1], base[2] + this.position[2]];
  }

  getScale(): Vec3 {
    const parent = this.parentTransform();
    if (!parent) {
      return this.scale;
    }

    const base = parent.getScale();
    return [base[0] * this.scale[0], base[1] * this.scale[1], base[2] * this.scale[2]];
  }

  getRotation(): Vec3 {
    const parent = this.parentTransform();
    if (!parent) {
      return this.rotation;
    }

    const base = parent.getRotation();
    return [base[0] + this.rotation[0], base[1] + this.rotation[1], base[2] + this.rotation[2]];
  }

  // Packed order is position, scale, rotation - NOT the position/rotation/scale order the JSON blob
  // and the state delta both use.
  unpack(reader: MessageReader): void {
    this.position = reader.readVec3();
    this.scale = reader.readVec3();
    this.rotation = reader.readVec3();
  }

  loadFromJSON(data: Record<string, unknown>): void {
    this.position = vec3FromJSON(data.position, this.position);
    this.rotation = vec3FromJSON(data.rotation, this.rotation);
    this.scale = vec3FromJSON(data.scale, this.scale);
  }

  private parentTransform(): Transform | null {
    const parent = this.owner?.getParent();
    return parent?.getComponent<Transform>(ComponentType.transform) ?? null;
  }
}
