// Port of renderObject/MousePicking.vert/.frag: draw each pickable object's ID into an
// rgba8uint offscreen target (ID packed into RGB bytes, exactly as upstream).
// Group 0 = camera (view/proj prefix of the shared frame uniform), group 1 = object,
// group 2 = pick ID (dynamic-offset uniform standing in for the push constant).

struct Camera {
  view: mat4x4<f32>,
  proj: mat4x4<f32>,
};

struct ObjectUniform {
  model: mat4x4<f32>,
  normalMatrix: mat4x4<f32>,
};

struct PickConstants {
  objectID: u32,
};

@group(0) @binding(0) var<uniform> camera: Camera;
@group(1) @binding(0) var<uniform> object: ObjectUniform;
@group(2) @binding(0) var<uniform> pick: PickConstants;

@vertex
fn vs_main(@location(0) position: vec3<f32>) -> @builtin(position) vec4<f32> {
  return camera.proj * camera.view * object.model * vec4<f32>(position, 1.0);
}

@fragment
fn fs_main() -> @location(0) vec4<u32> {
  let r = (pick.objectID >> 16u) & 0xFFu;
  let g = (pick.objectID >> 8u) & 0xFFu;
  let b = pick.objectID & 0xFFu;
  return vec4<u32>(r, g, b, 255u);
}
