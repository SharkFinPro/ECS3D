// Infinite ground grid — port of the engine's Grid.vert/Grid.frag approach: a fullscreen
// triangle whose fragments ray-cast the y=0 plane via the inverse view-projection, draw
// antialiased lines, and write real depth so the grid occludes correctly.

struct GridUniforms {
  viewProj : mat4x4<f32>,
  invViewProj : mat4x4<f32>,
  cameraPos : vec3<f32>,
};

@group(0) @binding(0) var<uniform> grid : GridUniforms;

struct VertexOut {
  @builtin(position) position : vec4<f32>,
  @location(0) nearPoint : vec3<f32>,
  @location(1) farPoint : vec3<f32>,
};

fn unproject(ndc : vec2<f32>, depth : f32) -> vec3<f32> {
  let p = grid.invViewProj * vec4<f32>(ndc, depth, 1.0);
  return p.xyz / p.w;
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex : u32) -> VertexOut {
  // Fullscreen triangle.
  var ndc = array<vec2<f32>, 3>(
    vec2<f32>(-1.0, -1.0),
    vec2<f32>( 3.0, -1.0),
    vec2<f32>(-1.0,  3.0),
  );
  var out : VertexOut;
  out.position = vec4<f32>(ndc[vertexIndex], 0.0, 1.0);
  out.nearPoint = unproject(ndc[vertexIndex], 0.0); // depth range 0..1
  out.farPoint = unproject(ndc[vertexIndex], 1.0);
  return out;
}

// Direct port of Grid.frag's gridColor(position, minor = 1, major = 10): red X axis, blue Z
// axis, white major lines every minor*major units (alpha 0.3), white minor lines every minor
// units (alpha 0.1).
fn gridColor(position : vec3<f32>, minor : f32, major : f32) -> vec4<f32> {
  let lineWidth = 1.0;
  let axisWidth = 2.0;

  // WGSL uniformity: all derivatives must be taken before any non-uniform branch.
  let axisDistance = abs(position.xz) / fwidth(position.xz);
  let majorPos = position.xz / (minor * major);
  let majorFw = fwidth(majorPos);
  let minorPos = position.xz / minor;
  let minorFw = fwidth(minorPos);

  // Axis Highlights
  let axisAlphaX = 1.0 - smoothstep(0.0, axisWidth, axisDistance.y);
  if (axisAlphaX > 0.0) {
    return vec4<f32>(1.0, 0.0, 0.0, 0.9 * axisAlphaX);
  }

  let axisAlphaZ = 1.0 - smoothstep(0.0, axisWidth, axisDistance.x);
  if (axisAlphaZ > 0.0) {
    return vec4<f32>(0.0, 0.0, 1.0, 0.9 * axisAlphaZ);
  }

  // Major Grid
  let majorGrid = abs(fract(majorPos - 0.5) - 0.5) / majorFw;
  let majorLine = min(majorGrid.x, majorGrid.y);
  let majorAlpha = 1.0 - smoothstep(0.0, lineWidth, majorLine);

  if (majorAlpha > 0.0) {
    return vec4<f32>(1.0, 1.0, 1.0, 0.3 * majorAlpha);
  }

  // Minor Grid
  let minorGrid = abs(fract(minorPos - 0.5) - 0.5) / minorFw;
  let minorLine = min(minorGrid.x, minorGrid.y);
  let minorAlpha = 1.0 - smoothstep(0.0, lineWidth, minorLine);

  if (minorAlpha > 0.0) {
    return vec4<f32>(1.0, 1.0, 1.0, 0.1 * minorAlpha);
  }

  return vec4<f32>(0.0);
}

struct FragOut {
  @location(0) color : vec4<f32>,
  @builtin(frag_depth) depth : f32,
};

@fragment
fn fs_main(in : VertexOut) -> FragOut {
  let dir = in.farPoint - in.nearPoint;
  let t = -in.nearPoint.y / dir.y;
  if (t <= 0.0 || abs(dir.y) < 1e-6) {
    discard;
  }
  let world = in.nearPoint + t * dir;

  let clip = grid.viewProj * vec4<f32>(world, 1.0);
  let depth = clip.z / clip.w;

  // Grid.frag: fadeout = exp(-dist * 0.0025), applied to the whole color (rgb and alpha).
  let dist = length(world - grid.cameraPos);
  let fade = exp(-dist * 0.0025);
  let color = gridColor(world, 1.0, 10.0) * fade;
  if (color.a < 0.003) {
    discard;
  }

  var out : FragOut;
  out.color = color;
  out.depth = depth;
  return out;
}
