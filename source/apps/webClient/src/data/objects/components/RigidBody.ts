// Port of source/libs/data/objects/components/RigidBody.{h,cpp}.
//
// Replicated but inert here: the client runs no PhysicsSystem, so these values only exist so the
// snapshot round-trips and a future inspector-style view could read them. Motion arrives as the
// per-tick state delta instead.

import type { MessageReader, Vec3 } from "../../../net/Protocol";
import { Component, ComponentType, vec3FromJSON } from "./Component";

export class RigidBody extends Component {
  private velocity: Vec3 = [0, 0, 0];
  private friction = 0.1;
  private doGravity = true;
  private gravity = -9.81;
  private angularVelocity: Vec3 = [0, 0, 0];
  private mass = 10;

  constructor() {
    super(ComponentType.rigidBody);
  }

  getVelocity(): Vec3 {
    return this.velocity;
  }

  getAngularVelocity(): Vec3 {
    return this.angularVelocity;
  }

  getMass(): number {
    return this.mass;
  }

  getFriction(): number {
    return this.friction;
  }

  getGravity(): number {
    return this.gravity;
  }

  getDoGravity(): boolean {
    return this.doGravity;
  }

  unpack(reader: MessageReader): void {
    this.velocity = reader.readVec3();
    this.friction = reader.readFloat();
    this.doGravity = reader.readBool();
    this.gravity = reader.readFloat();
    this.angularVelocity = reader.readVec3();
    this.mass = reader.readFloat();
  }

  loadFromJSON(data: Record<string, unknown>): void {
    this.velocity = vec3FromJSON(data.velocity, this.velocity);
    this.angularVelocity = vec3FromJSON(data.angularVelocity, this.angularVelocity);
    this.friction = Number(data.friction ?? this.friction);
    this.doGravity = Boolean(data.doGravity ?? this.doGravity);
    this.gravity = Number(data.gravity ?? this.gravity);
    this.mass = Number(data.mass ?? this.mass);
  }
}
