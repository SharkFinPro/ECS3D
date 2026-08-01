// Port of source/libs/data/objects/components/LightRenderer.{h,cpp}.
//
// Note the JSON key is "isSpotlight" (lowercase L) while the accessor is isSpotLight - matching
// upstream's serialize(), which the editComponent path parses.

import type { MessageReader, Vec3 } from "../../../net/Protocol";
import { Component, ComponentType, vec3FromJSON } from "./Component";

export class LightRenderer extends Component {
  private spotLight = false;
  private color: Vec3 = [0, 0, 0];
  private ambient = 0;
  private diffuse = 0;
  private specular = 0;
  private direction: Vec3 = [0, 0, 0];
  private coneAngle = 0;

  constructor() {
    super(ComponentType.lightRenderer);
  }

  isSpotLight(): boolean {
    return this.spotLight;
  }

  getColor(): Vec3 {
    return this.color;
  }

  getAmbient(): number {
    return this.ambient;
  }

  getDiffuse(): number {
    return this.diffuse;
  }

  getSpecular(): number {
    return this.specular;
  }

  getDirection(): Vec3 {
    return this.direction;
  }

  getConeAngle(): number {
    return this.coneAngle;
  }

  unpack(reader: MessageReader): void {
    this.spotLight = reader.readBool();

    this.color = reader.readVec3();
    this.ambient = reader.readFloat();
    this.diffuse = reader.readFloat();
    this.specular = reader.readFloat();

    this.direction = reader.readVec3();
    this.coneAngle = reader.readFloat();
  }

  loadFromJSON(data: Record<string, unknown>): void {
    this.color = vec3FromJSON(data.color, this.color);
    this.direction = vec3FromJSON(data.direction, this.direction);

    this.ambient = Number(data.ambient ?? this.ambient);
    this.diffuse = Number(data.diffuse ?? this.diffuse);
    this.specular = Number(data.specular ?? this.specular);
    this.coneAngle = Number(data.coneAngle ?? this.coneAngle);

    this.spotLight = Boolean(data.isSpotlight ?? this.spotLight);
  }
}
