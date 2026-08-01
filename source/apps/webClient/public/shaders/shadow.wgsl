// Shadow pass — port of the original ShadowCubeMap.vert/.frag: one cube face per pass,
// view-projection + light position delivered via a dynamic-offset uniform (one slot per
// light-face). Like the original, the stored depth is the LINEAR radial distance from the
// light divided by the far plane (100), not the projected depth; the object shader compares
// length(fragToLight)/far against it.

struct FaceUniform {
  viewProj : mat4x4<f32>,
  lightPos : vec4<f32>, // xyz = light position, w = far plane
};

struct ObjectUniform {
  model : mat4x4<f32>,
  normalMatrix : mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> face : FaceUniform;
@group(1) @binding(0) var<uniform> object : ObjectUniform;

struct VertexOut {
  @builtin(position) clipPosition : vec4<f32>,
  @location(0) worldPosition : vec3<f32>,
};

@vertex
fn vs_main(@location(0) position : vec3<f32>) -> VertexOut {
  var out : VertexOut;
  let world = object.model * vec4<f32>(position, 1.0);
  out.worldPosition = world.xyz;
  out.clipPosition = face.viewProj * world;
  return out;
}

@fragment
fn fs_main(in : VertexOut) -> @builtin(frag_depth) f32 {
  let dist = distance(in.worldPosition, face.lightPos.xyz);
  return clamp(dist / face.lightPos.w, 0.0, 1.0);
}
