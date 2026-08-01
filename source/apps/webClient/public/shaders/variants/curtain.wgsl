// Port of Curtain.vert + Curtain.frag: sinusoidal "curtain" vertex displacement in local Z,
// analytic surface normal, constant golden color lit by all point lights.
// Groups 0/1/2 match standard.wgsl; group 3 carries the curtain params (used in both stages).
//
// The period usage mirrors the original faithfully, including its inconsistency: the
// displacement uses (x * period) while the tangent derivatives use (x / period).

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
  // xyz = spot direction, w = cone angle in radians (w <= 0 -> point light)
  direction : vec4<f32>,
};

struct ObjectUniform {
  model : mat4x4<f32>,
  normalMatrix : mat4x4<f32>,
};

struct Params {
  amplitude : f32,
  period : f32,
  shininess : f32,
};

@group(0) @binding(0) var<uniform> camera : CameraUniform;
@group(0) @binding(1) var<storage, read> lights : array<PointLight>;

@group(2) @binding(0) var<uniform> object : ObjectUniform;

@group(3) @binding(0) var<uniform> params : Params;

const PI = 3.14;
const Y0 = 5.0;

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
  return out;
}

@fragment
fn fs_main(in : VertexOut) -> @location(0) vec4<f32> {
  let fragColor = vec3<f32>(0.855, 0.647, 0.125);
  var result = vec3<f32>(0.0);
  for (var i = 0u; i < camera.numLights; i++) {
    result += standardPointLightAffect(lights[i], fragColor, in.worldNormal, in.worldPosition,
                                       camera.cameraPos, params.shininess);
  }
  return vec4<f32>(result, 1.0);
}
