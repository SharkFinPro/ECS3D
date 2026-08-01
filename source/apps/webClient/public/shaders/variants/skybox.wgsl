// Skybox: a full-screen triangle whose per-pixel world-space ray direction is reconstructed
// from the inverse view-projection and used to sample the room cube map. Drawn at the far
// plane (clip z = w -> depth 1.0) with depth-write off and less-equal compare so it fills
// only where no geometry was written. Standalone bind scheme (not the standard frame layout).

struct SkyUniform {
  invViewProj : mat4x4<f32>,
  cameraPos : vec3<f32>,
  pad : f32,
};

@group(0) @binding(0) var<uniform> sky : SkyUniform;
@group(1) @binding(0) var cubeTexture : texture_cube<f32>;
@group(1) @binding(1) var cubeSampler : sampler;

struct VertexOut {
  @builtin(position) clipPosition : vec4<f32>,
  @location(0) ndc : vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) vi : u32) -> VertexOut {
  // Oversized triangle covering the viewport.
  var pts = array<vec2<f32>, 3>(
    vec2<f32>(-1.0, -3.0),
    vec2<f32>(-1.0, 1.0),
    vec2<f32>(3.0, 1.0),
  );
  let p = pts[vi];
  var out : VertexOut;
  out.ndc = p;
  out.clipPosition = vec4<f32>(p, 1.0, 1.0); // z = w => depth 1.0
  return out;
}

@fragment
fn fs_main(in : VertexOut) -> @location(0) vec4<f32> {
  let world = sky.invViewProj * vec4<f32>(in.ndc, 1.0, 1.0);
  let dir = normalize(world.xyz / world.w - sky.cameraPos);
  return vec4<f32>(textureSample(cubeTexture, cubeSampler, dir).rgb, 1.0);
}
