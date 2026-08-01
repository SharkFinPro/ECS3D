// Port of renderObject/ObjectHighlight.vert/.frag: the mesh scaled up 1% and drawn as a
// translucent green shell over hovered/selected objects.

struct Camera {
  view: mat4x4<f32>,
  proj: mat4x4<f32>,
};

struct ObjectUniform {
  model: mat4x4<f32>,
  normalMatrix: mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> camera: Camera;
@group(1) @binding(0) var<uniform> object: ObjectUniform;

@vertex
fn vs_main(@location(0) position: vec3<f32>) -> @builtin(position) vec4<f32> {
  let scaledPosition = position * 1.01;
  return camera.proj * camera.view * object.model * vec4<f32>(scaledPosition, 1.0);
}

@fragment
fn fs_main() -> @location(0) vec4<f32> {
  return vec4<f32>(0.2, 0.9, 0.2, 0.3);
}
