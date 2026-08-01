// Port of source/components/lighting/lights/Light.{h,cpp} — the abstract base for PointLight and
// SpotLight.
//
// DEVIATION: upstream gives every Light its own shadow-map ImageResource and its own uniform
// struct (PointLightUniform holds six view-projections, SpotLightUniform holds one). This port
// packs every light into ONE 64-byte slot of a shared storage buffer and renders every light's
// shadows into a shared depth cube-array (ShadowMaps.ts), because WGSL reads the whole light set
// as a single runtime-sized array. `writeUniform` below is that 64-byte ABI, and it is duplicated
// as the `PointLight` struct in eight WGSL files — keep them in sync.

import { Vec3 } from "../../../utilities/Math";

export enum LightType {
  pointLight = "pointLight",
  spotLight = "spotLight",
}

export interface CommonLightData {
  position: Vec3;
  color: Vec3;
  ambient: number;
  diffuse: number;
  specular: number;
}

// f32 count of one light's slot: position+pad, color+pad, (ambient,diffuse,specular)+pad,
// direction.xyz + coneAngle.
export const LIGHT_STRIDE_FLOATS = 16;

export abstract class Light {
  // Format of every shadow map; the shadow pipeline must declare the same depth attachment
  // format (upstream Light::s_shadowMapFormat = eD32Sfloat).
  static readonly shadowMapFormat: GPUTextureFormat = "depth32float";
  static readonly shadowMapExtent = { width: 1024, height: 1024 };

  protected position: Vec3;
  protected color: Vec3;
  protected ambient: number;
  protected diffuse: number;
  protected specular: number;

  protected castsShadowsFlag = true;

  protected constructor(data: CommonLightData) {
    this.position = [...data.position] as Vec3;
    this.color = [...data.color] as Vec3;
    this.ambient = data.ambient;
    this.diffuse = data.diffuse;
    this.specular = data.specular;
  }

  getPosition(): Vec3 {
    return [...this.position] as Vec3;
  }

  getColor(): Vec3 {
    return [...this.color] as Vec3;
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

  setPosition(position: Vec3): void {
    this.position = [...position] as Vec3;
  }

  setColor(color: Vec3): void {
    this.color = [...color] as Vec3;
  }

  setAmbient(ambient: number): void {
    this.ambient = ambient;
  }

  setDiffuse(diffuse: number): void {
    this.diffuse = diffuse;
  }

  setSpecular(specular: number): void {
    this.specular = specular;
  }

  getShadowMapExtent(): { width: number; height: number } {
    return Light.shadowMapExtent;
  }

  castsShadows(): boolean {
    return this.castsShadowsFlag;
  }

  abstract getLightType(): LightType;

  // Packs this light's 64-byte slot at `offset` f32s into the shared lights storage buffer —
  // the analogue of Light::getUniform().
  writeUniform(target: Float32Array, offset: number): void {
    target.set(this.position, offset);
    target.set(this.color, offset + 4);
    target.set([this.ambient, this.diffuse, this.specular], offset + 8);
    target.set([...this.getDirection(), this.getConeAngleRadians()], offset + 12);
  }

  // The .w component of the direction slot doubles as the point/spot discriminator: <= 0 means
  // "no cone", i.e. a point light (see isInSpotlight in the WGSL lighting helpers).
  protected abstract getDirection(): Vec3;

  protected abstract getConeAngleRadians(): number;
}
