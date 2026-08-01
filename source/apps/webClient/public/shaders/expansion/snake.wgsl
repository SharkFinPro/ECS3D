// Snake — reformulation of Snake.vert + Snake.geom + Snake.frag.
//
// The original geometry shader is an identity-topology pass (3 triangle verts in, 3 out): it
// only *displaces* each vertex along its normal, so no primitive amplification is needed. We
// fold the whole vert+geom chain into a single WGSL vertex shader operating on the source
// mesh drawn with the normal indexed triangle list (standard pos/normal/uv vertex buffer).
//
// Pipeline: proj*view applied here (geom did it); model applied in the "vert" half. The two
// wiggle displacements from vert (pos.z) and geom (along normal) are both reproduced.

struct Uniforms {
  view : mat4x4<f32>,
  proj : mat4x4<f32>,
  model : mat4x4<f32>,
  normalMatrix : mat4x4<f32>,
  cameraPos : vec3<f32>,
  numLights : u32,
  wiggle : f32,
  _p0 : f32,
  _p1 : f32,
  _p2 : f32,
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

struct VSOut {
  @builtin(position) clip : vec4<f32>,
  @location(0) fragPos : vec3<f32>,
  @location(1) uv : vec2<f32>,
  @location(2) fragNormal : vec3<f32>,
};

@vertex
fn vs_main(@location(0) inPos : vec3<f32>, @location(1) inNormal : vec3<f32>,
           @location(2) inUV : vec2<f32>) -> VSOut {
  // Snake.vert: pre-bend along z, then to world space.
  var pos = inPos;
  pos.z = pos.z + sin(pos.x * 0.5) * u.wiggle;

  let world0 = (u.model * vec4<f32>(pos, 1.0)).xyz;
  let worldNormal = (u.normalMatrix * vec4<f32>(inNormal, 0.0)).xyz;

  // Snake.geom: displace along the (unnormalized) normal by an animated, tension-scaled amount.
  let tension = abs(sin(world0.x * 0.5) * u.wiggle);
  let expansionFactor = 0.2 * tension;
  let displaced = world0 + worldNormal * sin(world0.x * 3.0 + u.wiggle * 5.0) * expansionFactor;

  var out : VSOut;
  out.clip = u.proj * u.view * vec4<f32>(displaced, 1.0);
  out.fragPos = displaced;
  out.uv = inUV;
  out.fragNormal = worldNormal;
  return out;
}

fn hsvToRgb(h : f32, s : f32, v : f32) -> vec3<f32> {
  let c = v * s;
  let x = c * (1.0 - abs((h * 6.0) % 2.0 - 1.0));
  let m = v - c;
  var rgb = vec3<f32>(0.0);
  if (h < 1.0 / 6.0) { rgb = vec3<f32>(c, x, 0.0); }
  else if (h < 2.0 / 6.0) { rgb = vec3<f32>(x, c, 0.0); }
  else if (h < 3.0 / 6.0) { rgb = vec3<f32>(0.0, c, x); }
  else if (h < 4.0 / 6.0) { rgb = vec3<f32>(0.0, x, c); }
  else if (h < 5.0 / 6.0) { rgb = vec3<f32>(x, 0.0, c); }
  else { rgb = vec3<f32>(c, 0.0, x); }
  return rgb + vec3<f32>(m);
}

// Port of Lighting.glsl StandardPointLightAffect (shininess = 10 in Snake.frag).
fn pointLightAffect(light : PointLight, color : vec3<f32>, normal : vec3<f32>,
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

@fragment
fn fs_main(in : VSOut) -> @location(0) vec4<f32> {
  let pos = in.fragPos.x + 11.2;
  let p = pos / 18.0;
  var color = hsvToRgb(mix(1.0 / 6.0, 5.0 / 6.0, p), 1.0, 1.0);

  let tension = abs(sin(in.fragPos.x * 0.5) * u.wiggle);
  color.g = color.g - tension;
  color.b = color.b - tension;
  color.r = color.r + tension;

  var result = vec3<f32>(0.0);
  for (var i = 0u; i < u.numLights; i = i + 1u) {
    result += pointLightAffect(lights[i], color, in.fragNormal, in.fragPos, u.cameraPos, 10.0);
  }
  return vec4<f32>(result, 1.0);
}
