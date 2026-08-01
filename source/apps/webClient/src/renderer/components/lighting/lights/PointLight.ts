// Port of source/components/lighting/lights/PointLight.{h,cpp}.
//
// Upstream this class also owns the six cube-face view-projection matrices and its own shadow
// cube image. Both move to the shared ShadowMaps (see Light.ts's deviation note); what remains
// here is the light type and the "not a spot light" half of the packed uniform.

import { Vec3 } from "../../../utilities/Math";
import { CommonLightData, Light, LightType } from "./Light";

export class PointLight extends Light {
  constructor(data: CommonLightData) {
    super(data);
  }

  getLightType(): LightType {
    return LightType.pointLight;
  }

  protected getDirection(): Vec3 {
    return [0, 0, 0];
  }

  // 0 => the shaders treat this light as omnidirectional.
  protected getConeAngleRadians(): number {
    return 0;
  }
}
