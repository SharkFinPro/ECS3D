// Bendy plants — port of Bendy.vert + Bendy.frag.
//
// Bendy.vert already used no vertex input: it procedurally builds a leaf quad-strip from
// gl_VertexIndex and rotates each blade ("fin") by gl_InstanceIndex, with a time-based sway.
// This is the same "instanced expansion" shape the geom reformulations use, so it ports
// almost verbatim. We fold the per-plant model matrix into instancing too: we draw
// numFins * numPlants instances and split instance_index into (plantIdx, finIdx), reading the
// plant's model matrix from a storage buffer — this lets both plants render in one draw call.

struct Uniforms {
  vp : mat4x4<f32>,
  time : f32,
  leafLength : i32,
  pitch : f32,
  bendStrength : f32,
  numFins : u32,
  numLights : u32,
  _p0 : f32,
  _p1 : f32,
};

struct PointLight {
  position : vec3<f32>,
  color : vec3<f32>,
  params : vec3<f32>,
  // xyz = spot direction, w = cone angle in radians (w <= 0 -> point light)
  direction : vec4<f32>,
};

@group(0) @binding(0) var<uniform> u : Uniforms;
@group(0) @binding(1) var<storage, read> lights : array<PointLight>;
@group(0) @binding(2) var<storage, read> plantModels : array<mat4x4<f32>>;
@group(0) @binding(3) var leafTexture : texture_2d<f32>;
@group(0) @binding(4) var leafSampler : sampler;

const YAW_DEGREES_PER_INSTANCE : f32 = 65.0;
const PITCH_OFFSET_PER_INSTANCE : f32 = 2.5;

struct VSOut {
  @builtin(position) clip : vec4<f32>,
  @location(0) uv : vec2<f32>,
  @location(1) fragPos : vec3<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) vi : u32, @builtin(instance_index) inst : u32) -> VSOut {
  let finIdx = i32(inst % u.numFins);
  let plantIdx = inst / u.numFins;
  let model = plantModels[plantIdx];

  // Quad-strip corner from the vertex index.
  let quadX = f32(vi % 2u) - 0.5;
  let quadY = f32(vi / 2u) / 4.0;

  let uv = vec2<f32>(f32(vi % 2u), 1.0 - quadY / f32(u.leafLength));

  let yawRadians = radians(YAW_DEGREES_PER_INSTANCE * f32(finIdx));
  var pitchRadians = radians(u.pitch - f32(finIdx) * PITCH_OFFSET_PER_INSTANCE);

  // Sway.
  pitchRadians = pitchRadians + radians(sin(u.time + f32((finIdx * 2) % 5)) * quadY * 1.25);

  let yawTrig = vec2<f32>(cos(yawRadians), sin(yawRadians));

  let bendPitchRadians = pitchRadians + quadY * u.bendStrength;
  let bendPitchTrig = vec2<f32>(cos(bendPitchRadians), sin(bendPitchRadians));

  let position = vec3<f32>(
    yawTrig.x * -quadX + bendPitchTrig.x * quadY * yawTrig.y,
    bendPitchTrig.y * quadY,
    yawTrig.y * quadX + bendPitchTrig.x * quadY * yawTrig.x,
  );

  let world = model * vec4<f32>(position, 1.0);

  var out : VSOut;
  out.clip = u.vp * world;
  out.uv = uv;
  out.fragPos = world.xyz;
  return out;
}

// Port of Lighting.glsl SmokePointLightAffect (distance attenuation, no view dependence).
fn smokePointLightAffect(light : PointLight, color : vec3<f32>, fragPos : vec3<f32>) -> vec3<f32> {
  let lightToFrag = light.position - fragPos;
  let dist = length(lightToFrag);
  let attenuation = 1.0 / (1.0 + 0.09 * dist + 0.032 * dist * dist);
  return (light.params.x + light.params.y) * color * light.color * attenuation;
}

@fragment
fn fs_main(in : VSOut) -> @location(0) vec4<f32> {
  let texColor = textureSample(leafTexture, leafSampler, in.uv);
  if (texColor.a < 0.1) {
    discard;
  }

  var result = vec3<f32>(0.0);
  for (var i = 0u; i < u.numLights; i = i + 1u) {
    result += smokePointLightAffect(lights[i], texColor.rgb, in.fragPos);
  }
  return vec4<f32>(result, texColor.a);
}
