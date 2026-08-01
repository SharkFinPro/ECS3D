// Port of source/components/lighting/lights/SpotLight.{h,cpp}.
//
// Same defaults as upstream: direction (0,-1,0), cone angle 15 degrees, hard cone cutoff (the
// isInSpotlight test in Lighting.glsl, mirrored in the ported WGSL).
//
// DEVIATION: upstream gives a spot light a single 2D shadow map; this port shadows it through the
// same depth cube face set as a point light. That is geometrically equivalent for occlusion — the
// cone cutoff is applied in the fragment shader either way.

import { Vec3 } from "../../../utilities/Math";
import { CommonLightData, Light, LightType } from "./Light";

export class SpotLight extends Light {
  private direction: Vec3 = [0, -1, 0];
  private coneAngleDegrees = 15;

  constructor(data: CommonLightData) {
    super(data);
  }

  getLightType(): LightType {
    return LightType.spotLight;
  }

  getDirectionVector(): Vec3 {
    return [...this.direction] as Vec3;
  }

  setDirection(direction: Vec3): void {
    this.direction = [...direction] as Vec3;
  }

  getConeAngle(): number {
    return this.coneAngleDegrees;
  }

  setConeAngle(coneAngleDegrees: number): void {
    this.coneAngleDegrees = coneAngleDegrees;
  }

  protected getDirection(): Vec3 {
    return this.direction;
  }

  protected getConeAngleRadians(): number {
    return (this.coneAngleDegrees * Math.PI) / 180;
  }
}
