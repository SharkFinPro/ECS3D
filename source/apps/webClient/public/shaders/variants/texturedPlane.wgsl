// Port of TexturedPlane.vert + TexturedPlane.frag: unlit diffuse-textured surface.
// Groups 0/1/2 match standard.wgsl (frame / material / object). No group 3.

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

@group(0) @binding(0) var<uniform> camera : CameraUniform;

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

@fragment
fn fs_main(in : VertexOut) -> @location(0) vec4<f32> {
  let texColor = textureSample(diffuseTexture, materialSampler, in.uv).rgb;
  return vec4<f32>(texColor, 1.0);
}
