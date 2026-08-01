// Port of StandardObject.vert + EllipticalDots.frag: procedural ellipse-tiled color, then
// Phong over all point lights (faithful to Lighting.glsl's StandardPointLightAffect quirk of
// multiplying the accumulated term by light.color again). Groups 0/1/2 match standard.wgsl;
// group 3 carries the push-constant params.

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
  params : vec3<f32>, // x=ambient, y=diffuse, z=specular
  direction : vec4<f32>, // xyz = spot direction, w = cone angle radians (<=0 -> point)
};

struct ObjectUniform {
  model : mat4x4<f32>,
  normalMatrix : mat4x4<f32>,
};

struct Params {
  shininess : f32,
  sDiameter : f32,
  tDiameter : f32,
  blendFactor : f32,
};

@group(0) @binding(0) var<uniform> camera : CameraUniform;
@group(0) @binding(1) var<storage, read> lights : array<PointLight>;

@group(2) @binding(0) var<uniform> object : ObjectUniform;

@group(3) @binding(0) var<uniform> params : Params;

const OBJECTCOLOR = vec3<f32>(1.0, 1.0, 1.0);
const ELLIPSECOLOR = vec3<f32>(0.6235, 0.8863, 0.7490);

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
  let s = in.uv.x;
  let t = in.uv.y;

  let numins = floor(s / params.sDiameter);
  let numint = floor(t / params.tDiameter);

  let Ar = params.sDiameter / 2.0;
  let Br = params.tDiameter / 2.0;

  let sc = numins * params.sDiameter + Ar;
  let tc = numint * params.tDiameter + Br;

  // x*x rather than pow(x, 2.0): pow() with a negative base is undefined in WGSL (would NaN).
  let es = (s - sc) / Ar;
  let et = (t - tc) / Br;
  let dist = es * es + et * et;
  let blend = smoothstep(1.0 - params.blendFactor, 1.0 + params.blendFactor, dist);
  let fragColor = mix(ELLIPSECOLOR, OBJECTCOLOR, blend);

  var result = vec3<f32>(0.0);
  for (var i = 0u; i < camera.numLights; i++) {
    result += standardPointLightAffect(lights[i], fragColor, in.worldNormal, in.worldPosition,
                                       camera.cameraPos, params.shininess);
  }
  return vec4<f32>(result, 1.0);
}
