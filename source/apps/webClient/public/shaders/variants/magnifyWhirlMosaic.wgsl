// Port of StandardObject.vert + MagnifyWhirlMosaic.frag: inside a circular lens the UVs are
// magnified, whirled and mosaic-quantized before sampling the diffuse texture; outside the
// lens the texture is sampled unchanged. Unlit. Groups 0/1/2 match standard.wgsl; group 3
// holds the lens params.

struct CameraUniform {
  view : mat4x4<f32>,
  proj : mat4x4<f32>,
  cameraPos : vec3<f32>,
  numLights : u32,
  shadowParams : vec4<f32>,
};

struct ObjectUniform {
  model : mat4x4<f32>,
  normalMatrix : mat4x4<f32>,
};

struct Params {
  lensS : f32,
  lensT : f32,
  lensRadius : f32,
  magnification : f32,
  whirl : f32,
  mosaic : f32,
};

@group(0) @binding(0) var<uniform> camera : CameraUniform;

@group(1) @binding(0) var materialSampler : sampler;
@group(1) @binding(1) var diffuseTexture : texture_2d<f32>;
@group(1) @binding(2) var specularTexture : texture_2d<f32>;

@group(2) @binding(0) var<uniform> object : ObjectUniform;

@group(3) @binding(0) var<uniform> params : Params;

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

@fragment
fn fs_main(in : VertexOut) -> @location(0) vec4<f32> {
  let lensST = vec2<f32>(params.lensS, params.lensT);

  var st = in.uv - lensST;
  let r = length(st);
  let insideLens = r <= params.lensRadius;

  // Magnify
  let rp = r / params.magnification;

  // Whirl
  let theta = atan2(st.y, st.x);
  let thetap = theta - params.whirl * rp;

  // Magnify + Whirl
  st = rp * vec2<f32>(cos(thetap), sin(thetap));

  // Restore coordinates
  st = st + lensST;

  // Mosaic (integer truncation toward zero, matching int() in the original)
  let numins = f32(i32(st.x / params.mosaic));
  let numint = f32(i32(st.y / params.mosaic));
  st = vec2<f32>(numins * params.mosaic, numint * params.mosaic);

  // Single sample at uniform control flow (WGSL forbids textureSample in the branch).
  let sampleUV = select(in.uv, st, insideLens);
  return vec4<f32>(textureSample(diffuseTexture, materialSampler, sampleUV).rgb, 1.0);
}
