// Standard lit object shader — port of StandardObject.vert + objects.frag: Phong over every
// queued light with a specular map, shadowed by the shared depth cube-array. Bind scheme mirrors
// the port contract: group 0 = lighting/frame, group 1 = material, group 2 = per-object.

struct CameraUniform {
  view : mat4x4<f32>,
  proj : mat4x4<f32>,
  cameraPos : vec3<f32>,
  numLights : u32,
  // x = shadow near, y = shadow far, z = number of shadow-casting lights
  shadowParams : vec4<f32>,
};

struct PointLight {
  position : vec3<f32>,
  color : vec3<f32>,
  // x = ambient, y = diffuse, z = specular intensity
  params : vec3<f32>,
  // xyz = spot direction, w = cone angle in radians (w <= 0 -> point light)
  direction : vec4<f32>,
};

struct ObjectUniform {
  model : mat4x4<f32>,
  normalMatrix : mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> camera : CameraUniform;
@group(0) @binding(1) var<storage, read> lights : array<PointLight>;
@group(0) @binding(2) var shadowMaps : texture_depth_cube_array;
@group(0) @binding(3) var shadowSampler : sampler_comparison;

// Shadow test for point light i — port of the original objects.frag point-light shadow:
// the cube map stores LINEAR radial distance / far (ShadowCubeMap.frag), so the reference
// is length(fragToLight) / far minus the original's 0.001 bias.
fn shadowFactor(lightIndex : u32, worldPosition : vec3<f32>) -> f32 {
  if (f32(lightIndex) >= camera.shadowParams.z) {
    return 1.0;
  }
  let toFrag = worldPosition - lights[lightIndex].position;
  let far = camera.shadowParams.y;
  let refDepth = length(toFrag) / far - 0.001;
  return textureSampleCompareLevel(shadowMaps, shadowSampler, toFrag, lightIndex, refDepth);
}

@group(1) @binding(0) var materialSampler : sampler;
@group(1) @binding(1) var diffuseTexture : texture_2d<f32>;
@group(1) @binding(2) var specularTexture : texture_2d<f32>;

@group(2) @binding(0) var<uniform> object : ObjectUniform;

struct VertexIn {
  @location(0) position : vec3<f32>,
  @location(1) normal : vec3<f32>,
  @location(2) uv : vec2<f32>,
};

struct VertexOut {
  @builtin(position) clipPosition : vec4<f32>,
  @location(0) worldPosition : vec3<f32>,
  @location(1) worldNormal : vec3<f32>,
  @location(2) uv : vec2<f32>,
};

@vertex
fn vs_main(in : VertexIn) -> VertexOut {
  var out : VertexOut;
  let world = object.model * vec4<f32>(in.position, 1.0);
  out.worldPosition = world.xyz;
  out.worldNormal = normalize((object.normalMatrix * vec4<f32>(in.normal, 0.0)).xyz);
  out.uv = in.uv;
  out.clipPosition = camera.proj * camera.view * world;
  return out;
}

// ---------------------------------------------------------------------------------------------
// Port of common/Lighting.glsl. Kept as separate helpers with upstream's names and argument
// order so the two can be diffed line by line.
//
// A light's `params` packs upstream's scalars: x = ambient, y = diffuse, z = specular.
// `direction.w` is the cone half-angle in radians and doubles as the point/spot discriminator
// (w <= 0 -> point light), because this port stores both kinds in one array rather than
// upstream's separate PointLights/SpotLights buffers.
// ---------------------------------------------------------------------------------------------

fn isInSpotlight(light : PointLight, fragPos : vec3<f32>) -> bool {
  let cutoffAngle = cos(light.direction.w);
  let lightToFrag = normalize(fragPos - light.position);
  let theta = dot(lightToFrag, normalize(light.direction.xyz));
  return theta >= cutoffAngle;
}

fn getStandardAmbient(lightAmbient : f32, color : vec3<f32>) -> vec3<f32> {
  return lightAmbient * color;
}

fn getStandardDiffuse(lightPosition : vec3<f32>, lightDiffuse : f32, fragPos : vec3<f32>,
                      normal : vec3<f32>, color : vec3<f32>) -> vec3<f32> {
  let lightDir = normalize(lightPosition - fragPos);
  let d = max(dot(normal, lightDir), 0.0);
  return lightDiffuse * d * color;
}

fn getStandardSpecular(lightPosition : vec3<f32>, lightSpecular : f32, lightColor : vec3<f32>,
                       cameraPosition : vec3<f32>, fragPos : vec3<f32>, normal : vec3<f32>,
                       shininess : f32) -> vec3<f32> {
  var specular = vec3<f32>(0.0);

  let lightDir = normalize(lightPosition - fragPos);
  let d = max(dot(normal, lightDir), 0.0);
  if (d > 0.0) { // only do specular if the light can see the point
    let viewDir = normalize(cameraPosition - fragPos);
    let reflectDir = normalize(reflect(-lightDir, normal));
    let cosphi = dot(viewDir, reflectDir);

    if (cosphi > 0.0) {
      specular = pow(cosphi, shininess) * lightSpecular * lightColor;
    }
  }

  return specular;
}

// NOTE: getStandardSpecular already folds in lightColor, and the return multiplies the whole sum
// by light.color again — so the specular term carries light.color SQUARED. That is upstream's
// behaviour (Lighting.glsl SpecularMapPointLightAffect), and every other shader in this port
// reproduces it; do not "simplify" it away.
fn specularMapPointLightAffect(light : PointLight, color : vec3<f32>, specColor : vec3<f32>,
                               normal : vec3<f32>, fragPos : vec3<f32>, cameraPosition : vec3<f32>,
                               shininess : f32) -> vec3<f32> {
  let normalizedNormal = normalize(normal);

  let ambient = getStandardAmbient(light.params.x, color);
  let diffuse = getStandardDiffuse(light.position, light.params.y, fragPos, normalizedNormal, color);
  let specular = getStandardSpecular(light.position, light.params.z, light.color, cameraPosition,
                                     fragPos, normalizedNormal, shininess) * specColor;

  return (ambient + diffuse + specular) * light.color;
}

fn specularMapSpotLightAffect(light : PointLight, color : vec3<f32>, specColor : vec3<f32>,
                              normal : vec3<f32>, fragPos : vec3<f32>, cameraPosition : vec3<f32>,
                              shininess : f32) -> vec3<f32> {
  if (!isInSpotlight(light, fragPos)) {
    // Out of cone: ambient only, and unlike the shadowed fallback in fs_main this one IS tinted
    // by light.color — mirroring upstream exactly.
    return getStandardAmbient(light.params.x, color) * light.color;
  }

  let normalizedNormal = normalize(normal);

  let ambient = getStandardAmbient(light.params.x, color);
  let diffuse = getStandardDiffuse(light.position, light.params.y, fragPos, normalizedNormal, color);
  let specular = getStandardSpecular(light.position, light.params.z, light.color, cameraPosition,
                                     fragPos, normalizedNormal, shininess) * specColor;

  return (ambient + diffuse + specular) * light.color;
}

@fragment
fn fs_main(in : VertexOut) -> @location(0) vec4<f32> {
  let texColor = textureSample(diffuseTexture, materialSampler, in.uv).rgb;
  let specColor = textureSample(specularTexture, materialSampler, in.uv).rgb;

  // Port of objects.frag's main(). Upstream runs two loops over separate point/spot buffers with
  // thresholds 0.1 and 0.5; this port has one array and one cube-shadow path for both kinds, so
  // there is a single loop at the point-light threshold of 0.1. The test stays BINARIZED — fully
  // lit, or ambient only — which is also what keeps the comparison filter from speckling.
  var result = vec3<f32>(0.0);
  for (var i = 0u; i < camera.numLights; i++) {
    let light = lights[i];
    let shadow = shadowFactor(i, in.worldPosition);

    if (shadow > 0.1) {
      if (light.direction.w > 0.0) {
        result += specularMapSpotLightAffect(light, texColor, specColor, in.worldNormal,
                                             in.worldPosition, camera.cameraPos, 32.0);
      } else {
        result += specularMapPointLightAffect(light, texColor, specColor, in.worldNormal,
                                              in.worldPosition, camera.cameraPos, 32.0);
      }
    } else {
      // Untinted by light.color — upstream's shadowed fallback deliberately differs from the lit
      // and out-of-cone paths here.
      result += getStandardAmbient(light.params.x, texColor);
    }
  }

  return vec4<f32>(result, 1.0);
}
