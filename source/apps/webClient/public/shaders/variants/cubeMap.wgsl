// Port of StandardObject.vert + CubeMap.frag: environment reflection/refraction sampled from
// a room cube map, with a noise-perturbed normal, blended between refract and reflect.
// Groups 0/1/2 match standard.wgsl; group 3 = params + noise volume + cube map.
// The eye position uses camera.cameraPos (the original fed the camera position via a push
// constant).

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
  mixFactor : f32,
  refractionIndex : f32,
  whiteMix : f32,
  noiseAmplitude : f32,
  noiseFrequency : f32,
};

@group(0) @binding(0) var<uniform> camera : CameraUniform;

@group(2) @binding(0) var<uniform> object : ObjectUniform;

@group(3) @binding(0) var<uniform> params : Params;
@group(3) @binding(1) var noiseTexture : texture_3d<f32>;
@group(3) @binding(2) var noiseSampler : sampler;
@group(3) @binding(3) var cubeTexture : texture_cube<f32>;
@group(3) @binding(4) var cubeSampler : sampler;

const WHITE = vec3<f32>(1.0, 1.0, 1.0);

fn perturbNormal2(angx : f32, angy : f32, nIn : vec3<f32>) -> vec3<f32> {
  var n = nIn;
  let cx = cos(angx);
  let sx = sin(angx);
  let cy = cos(angy);
  let sy = sin(angy);

  let yp = n.y * cx - n.z * sx;
  n.z = n.y * sx + n.z * cx;
  n.y = yp;

  let xp = n.x * cy + n.z * sy;
  n.z = -n.x * sy + n.z * cy;
  n.x = xp;

  return normalize(n);
}

fn perturbNormal3(angx : f32, angy : f32, angz : f32, nIn : vec3<f32>) -> vec3<f32> {
  var n = perturbNormal2(angx, angy, nIn);
  let cz = cos(angz);
  let sz = sin(angz);
  let zp = n.x * cz - n.y * sz;
  n.y = n.x * sz + n.y * cz;
  n.x = zp;
  return normalize(n);
}

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
  // Normal-matrix columns forwarded to the fragment stage (the shared object bind group
  // layout is vertex-only, so the fragment cannot read the object uniform directly).
  @location(3) nm0 : vec3<f32>,
  @location(4) nm1 : vec3<f32>,
  @location(5) nm2 : vec3<f32>,
};

@vertex
fn vs_main(in : VertexIn) -> VertexOut {
  var out : VertexOut;
  let world = object.model * vec4<f32>(in.position, 1.0);
  out.worldPosition = world.xyz;
  out.worldNormal = normalize((object.normalMatrix * vec4<f32>(in.normal, 0.0)).xyz);
  out.uv = in.uv;
  out.nm0 = object.normalMatrix[0].xyz;
  out.nm1 = object.normalMatrix[1].xyz;
  out.nm2 = object.normalMatrix[2].xyz;
  out.clipPosition = camera.proj * camera.view * world;
  return out;
}

@fragment
fn fs_main(in : VertexOut) -> @location(0) vec4<f32> {
  var normal = normalize(in.worldNormal);
  let eye = normalize(in.worldPosition - camera.cameraPos);

  let nvx = textureSample(noiseTexture, noiseSampler, params.noiseFrequency * in.worldPosition);
  let nvy = textureSample(noiseTexture, noiseSampler,
                          params.noiseFrequency * vec3<f32>(in.worldPosition.xy, in.worldPosition.z + 0.33));
  let nvz = textureSample(noiseTexture, noiseSampler,
                          params.noiseFrequency * vec3<f32>(in.worldPosition.xy, in.worldPosition.z + 0.67));

  let angx = (nvx.r + nvx.g + nvx.b + nvx.a - 2.0) * params.noiseAmplitude;
  let angy = (nvy.r + nvy.g + nvy.b + nvy.a - 2.0) * params.noiseAmplitude;
  let angz = (nvz.r + nvz.g + nvz.b + nvz.a - 2.0) * params.noiseAmplitude;

  normal = perturbNormal3(angx, angy, angz, normal);
  normal = normalize(mat3x3<f32>(in.nm0, in.nm1, in.nm2) * normal);

  let reflectVector = reflect(eye, normal);
  let reflectColor = textureSample(cubeTexture, cubeSampler, reflectVector).rgb;

  let refractVector = refract(eye, normal, params.refractionIndex);

  // Total internal reflection yields a zero refract vector; sample at uniform control
  // flow with a safe direction and select afterwards (WGSL uniformity rule).
  let totalInternal = all(refractVector == vec3<f32>(0.0, 0.0, 0.0));
  let safeRefract = select(refractVector, reflectVector, totalInternal);
  let refractSample = mix(textureSample(cubeTexture, cubeSampler, safeRefract).rgb, WHITE, params.whiteMix);
  let refractColor = select(refractSample, reflectColor, totalInternal);

  return vec4<f32>(mix(refractColor, reflectColor, params.mixFactor), 1.0);
}
