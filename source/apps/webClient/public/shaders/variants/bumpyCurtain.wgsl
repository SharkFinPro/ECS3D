// Port of Curtain.vert + BumpyCurtain.frag: curtain displacement, then the surface normal is
// noise-perturbed (PerturbNormal2) and transformed to world space before Phong lighting.
// Groups 0/1/2 match standard.wgsl; group 3 = curtain/noise params + noise volume.

struct CameraUniform {
  view : mat4x4<f32>,
  proj : mat4x4<f32>,
  cameraPos : vec3<f32>,
  numLights : u32,
  shadowParams : vec4<f32>,
};

struct PointLight {
  position : vec3<f32>,
  color : vec3<f32>,
  params : vec3<f32>,
  direction : vec4<f32>, // xyz = spot direction, w = cone angle radians (<=0 -> point)
};

struct ObjectUniform {
  model : mat4x4<f32>,
  normalMatrix : mat4x4<f32>,
};

struct Params {
  amplitude : f32,
  period : f32,
  shininess : f32,
  noiseAmplitude : f32,
  noiseFrequency : f32,
};

@group(0) @binding(0) var<uniform> camera : CameraUniform;
@group(0) @binding(1) var<storage, read> lights : array<PointLight>;

@group(2) @binding(0) var<uniform> object : ObjectUniform;

@group(3) @binding(0) var<uniform> params : Params;
@group(3) @binding(1) var noiseTexture : texture_3d<f32>;
@group(3) @binding(2) var noiseSampler : sampler;

const PI = 3.14;
const Y0 = 5.0;

fn perturbNormal2(angx : f32, angy : f32, nIn : vec3<f32>) -> vec3<f32> {
  var n = nIn;
  let cx = cos(angx);
  let sx = sin(angx);
  let cy = cos(angy);
  let sy = sin(angy);

  // rotate about x
  let yp = n.y * cx - n.z * sx;
  n.z = n.y * sx + n.z * cx;
  n.y = yp;

  // rotate about y
  let xp = n.x * cy + n.z * sy;
  n.z = -n.x * sy + n.z * cy;
  n.x = xp;

  return normalize(n);
}

fn standardPointLightAffect(light : PointLight, color : vec3<f32>, normal : vec3<f32>,
                            fragPos : vec3<f32>, camPos : vec3<f32>, shininess : f32) -> vec3<f32> {
  let n = normalize(normal);
  let ambient = light.params.x * color;
  let lightDir = normalize(light.position - fragPos);
  let d = max(dot(n, lightDir), 0.0);
  let diffuse = light.params.y * d * color;
  var specular = vec3<f32>(0.0);
  if (d > 0.0) {
    let viewDir = normalize(camPos - fragPos);
    let reflectDir = normalize(reflect(-lightDir, n));
    let cosphi = dot(viewDir, reflectDir);
    if (cosphi > 0.0) {
      specular = pow(cosphi, shininess) * light.params.z * light.color;
    }
  }
  return (ambient + diffuse + specular) * light.color;
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
  // Normal-matrix columns forwarded to the fragment stage (shared object BGL is
  // vertex-only, so the fragment cannot read the object uniform directly).
  @location(3) nm0 : vec3<f32>,
  @location(4) nm1 : vec3<f32>,
  @location(5) nm2 : vec3<f32>,
};

@vertex
fn vs_main(in : VertexIn) -> VertexOut {
  var out : VertexOut;
  var pos = in.position;
  pos.z = params.amplitude * (Y0 - pos.y) * sin(2.0 * PI * pos.x * params.period);

  let dzdx = params.amplitude * (Y0 - pos.y) * (2.0 * PI / params.period) * cos(2.0 * PI * pos.x / params.period);
  let dzdy = -params.amplitude * sin(2.0 * PI * pos.x / params.period);
  let Tx = vec3<f32>(1.0, 0.0, dzdx);
  let Ty = vec3<f32>(0.0, 1.0, dzdy);

  let world = object.model * vec4<f32>(pos, 1.0);
  out.worldPosition = world.xyz;
  out.worldNormal = normalize(cross(Tx, Ty));
  out.uv = in.uv;
  out.clipPosition = camera.proj * camera.view * world;
  out.nm0 = object.normalMatrix[0].xyz;
  out.nm1 = object.normalMatrix[1].xyz;
  out.nm2 = object.normalMatrix[2].xyz;
  return out;
}

@fragment
fn fs_main(in : VertexOut) -> @location(0) vec4<f32> {
  let nvx = textureSample(noiseTexture, noiseSampler, params.noiseFrequency * in.worldPosition);
  var angx = nvx.r + nvx.g + nvx.b + nvx.a - 2.0;
  angx = angx * params.noiseAmplitude;

  let nvy = textureSample(noiseTexture, noiseSampler,
                          params.noiseFrequency * vec3<f32>(in.worldPosition.xy, in.worldPosition.z + 0.5));
  var angy = nvy.r + nvy.g + nvy.b + nvy.a - 2.0;
  angy = angy * params.noiseAmplitude;

  var n = perturbNormal2(angx, angy, in.worldNormal);
  n = normalize(mat3x3<f32>(in.nm0, in.nm1, in.nm2) * n);

  let fragColor = vec3<f32>(1.0, 1.0, 1.0);
  var result = vec3<f32>(0.0);
  for (var i = 0u; i < camera.numLights; i++) {
    result += standardPointLightAffect(lights[i], fragColor, n, in.worldPosition,
                                       camera.cameraPos, params.shininess);
  }
  return vec4<f32>(result, 1.0);
}
