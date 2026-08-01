// Crosses — reformulation of Crosses.vert + Crosses.geom + Crosses.frag.
//
// The original geometry shader takes each source triangle, barycentrically subdivides it
// into a triangular grid of points (numLayers = 1 << level), and at every point emits three
// independent line segments (an axis-aligned cross along model X, Y, Z of half-size `size`).
//
// WebGPU has no geometry stage, so we draw line-list with instancing:
//   instances       = source triangle count       (instance_index -> triangle)
//   verts/instance  = numPoints * 6               (6 line verts = 3 segments per grid point)
// The source triangles (position + normal per vertex) live in a read-only storage buffer,
// pulled by instance_index; vertex_index selects the grid point and which cross endpoint.

struct Uniforms {
  view : mat4x4<f32>,
  proj : mat4x4<f32>,
  model : mat4x4<f32>,
  normalMatrix : mat4x4<f32>,
  cameraPos : vec3<f32>,
  numLights : u32,
  size : f32,
  quantize : f32,
  level : i32,
  useChromaDepth : u32,
  blueDepth : f32,
  redDepth : f32,
  shininess : f32,
  _pad : f32,
};

struct PointLight {
  position : vec3<f32>,
  color : vec3<f32>,
  // x = ambient, y = diffuse, z = specular
  params : vec3<f32>,
  // xyz = spot direction, w = cone angle in radians (w <= 0 -> point light)
  direction : vec4<f32>,
};

struct SrcVertex {
  position : vec3<f32>,
  normal : vec3<f32>,
};

@group(0) @binding(0) var<uniform> u : Uniforms;
@group(0) @binding(1) var<storage, read> lights : array<PointLight>;
@group(0) @binding(2) var<storage, read> src : array<SrcVertex>;

struct VSOut {
  @builtin(position) clip : vec4<f32>,
  @location(0) fragPos : vec3<f32>,
  @location(1) fragNormal : vec3<f32>,
  @location(2) fragZ : f32,
};

fn quantize(v : vec3<f32>) -> vec3<f32> {
  let q = u.quantize;
  let x = f32(i32(v.x * q)) / q;
  let y = f32(i32(v.y * q)) / q;
  let z = f32(i32(v.z * q)) / q;
  return vec3<f32>(x, y, z);
}

// Reproduces the geom's double loop to recover the (s, t) barycentric coords of grid point p.
fn barycentric(p : i32, numLayers : i32) -> vec2<f32> {
  var count = 0;
  let dt = 1.0 / f32(numLayers);
  var t = 1.0;
  for (var i = 0; i <= numLayers; i = i + 1) {
    let smax = 1.0 - t;
    let nums = i + 1;
    var ds = 0.0;
    if (nums > 1) {
      ds = smax / f32(nums - 1);
    }
    var s = 0.0;
    for (var is = 0; is < nums; is = is + 1) {
      if (count == p) {
        return vec2<f32>(s, t);
      }
      count = count + 1;
      s = s + ds;
    }
    t = t - dt;
  }
  return vec2<f32>(0.0, 0.0);
}

@vertex
fn vs_main(@builtin(vertex_index) vi : u32, @builtin(instance_index) inst : u32) -> VSOut {
  let base = inst * 3u;
  let V0 = src[base + 0u].position;
  let V1 = src[base + 1u].position;
  let V2 = src[base + 2u].position;
  let N0 = src[base + 0u].normal;
  let N1 = src[base + 1u].normal;
  let N2 = src[base + 2u].normal;

  let pointIdx = i32(vi / 6u);
  let cv = vi % 6u;

  let numLayers = i32(1u << u32(u.level));
  let st = barycentric(pointIdx, numLayers);
  let s = st.x;
  let t = st.y;

  var v = V0 + s * (V1 - V0) + t * (V2 - V0);
  v = quantize(v);

  let n = normalize((1.0 - s - t) * N0 + s * N1 + t * N2);
  let nv = normalize((u.normalMatrix * vec4<f32>(n, 0.0)).xyz);

  let ec = u.view * u.model * vec4<f32>(v, 1.0);
  let z = -ec.z;

  let sz = u.size;
  var offset = vec3<f32>(0.0);
  if (cv == 0u) { offset = vec3<f32>(-sz, 0.0, 0.0); }
  else if (cv == 1u) { offset = vec3<f32>(sz, 0.0, 0.0); }
  else if (cv == 2u) { offset = vec3<f32>(0.0, -sz, 0.0); }
  else if (cv == 3u) { offset = vec3<f32>(0.0, sz, 0.0); }
  else if (cv == 4u) { offset = vec3<f32>(0.0, 0.0, -sz); }
  else { offset = vec3<f32>(0.0, 0.0, sz); }

  let endpoint = v + offset;

  var out : VSOut;
  out.clip = u.proj * u.view * u.model * vec4<f32>(endpoint, 1.0);
  out.fragPos = endpoint;
  out.fragNormal = nv;
  out.fragZ = z;
  return out;
}

// Port of Lighting.glsl StandardPointLightAffect.
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

fn rainbow(tIn : f32) -> vec3<f32> {
  let t = clamp(tIn, 0.0, 1.0);
  var r = 1.0;
  var g = 0.0;
  var b = 1.0 - 6.0 * (t - (5.0 / 6.0));
  if (t <= (5.0 / 6.0)) { r = 6.0 * (t - (4.0 / 6.0)); g = 0.0; b = 1.0; }
  if (t <= (4.0 / 6.0)) { r = 0.0; g = 1.0 - 6.0 * (t - (3.0 / 6.0)); b = 1.0; }
  if (t <= (3.0 / 6.0)) { r = 0.0; g = 1.0; b = 6.0 * (t - (2.0 / 6.0)); }
  if (t <= (2.0 / 6.0)) { r = 1.0 - 6.0 * (t - (1.0 / 6.0)); g = 1.0; b = 0.0; }
  if (t <= (1.0 / 6.0)) { r = 1.0; g = 6.0 * t; }
  return vec3<f32>(r, g, b);
}

@fragment
fn fs_main(in : VSOut) -> @location(0) vec4<f32> {
  var color = vec3<f32>(0.8);
  if (u.useChromaDepth == 1u) {
    var tt = (2.0 / 3.0) * (abs(in.fragZ) - u.redDepth) / (u.blueDepth - u.redDepth);
    tt = clamp(tt, 0.0, 2.0 / 3.0);
    color = rainbow(tt);
  }

  var result = vec3<f32>(0.0);
  for (var i = 0u; i < u.numLights; i = i + 1u) {
    result += pointLightAffect(lights[i], color, in.fragNormal, in.fragPos, u.cameraPos, u.shininess);
  }
  return vec4<f32>(result, 1.0);
}
