// Renderer2D primitive shaders — port of source/shaders/2D/{Rect,Triangle,Ellipse}.{vert,frag}.
// The original pushes one (transform, bounds, color) push-constant block per draw call;
// here the same data rides in an instance vertex buffer instead (WebGPU has no portable
// push-constant path), letting all queued rects/triangles/ellipses draw in one instanced
// call per primitive kind.
//
// Coordinates are pixels with the origin at the TOP-LEFT of the canvas (Processing-style),
// same as the original. Vulkan NDC is Y-down so `ndc.y = 2y/h - 1` was enough there; WebGPU
// NDC is Y-up, so we additionally flip: `ndc.y = 1 - 2y/h`.

struct ScreenUniform {
  width : f32,
  height : f32,
  pad0 : f32,
  pad1 : f32,
};

@group(0) @binding(0) var<uniform> screen : ScreenUniform;

fn toNdc(worldPos : vec2<f32>) -> vec2<f32> {
  return vec2<f32>(
    2.0 * worldPos.x / screen.width - 1.0,
    1.0 - 2.0 * worldPos.y / screen.height,
  );
}

fn applyTransform(matAB : vec4<f32>, transTZ : vec4<f32>, p : vec2<f32>) -> vec2<f32> {
  // matAB = (a, b, c, d), transTZ.xy = (tx, ty) — 2D affine, column-vector convention:
  // x' = a*x + c*y + tx ; y' = b*x + d*y + ty
  return vec2<f32>(
    matAB.x * p.x + matAB.z * p.y + transTZ.x,
    matAB.y * p.x + matAB.w * p.y + transTZ.y,
  );
}

// ---------------------------------------------------------------------------
// Rect (port of Rect.vert / Rect.frag)
// ---------------------------------------------------------------------------

struct RectVertexIn {
  @builtin(vertex_index) vertexIndex : u32,
  @location(0) matAB : vec4<f32>,
  @location(1) transTZ : vec4<f32>, // (tx, ty, z, unused)
  @location(2) bounds : vec4<f32>,  // (x, y, width, height) — top-left origin
  @location(3) color : vec4<f32>,
};

struct RectVertexOut {
  @builtin(position) position : vec4<f32>,
  @location(0) color : vec4<f32>,
};

@vertex
fn vs_rect(in : RectVertexIn) -> RectVertexOut {
  var local : vec2<f32>;
  switch (in.vertexIndex) {
    case 0u: { local = vec2<f32>(in.bounds.x, in.bounds.y); }
    case 1u: { local = vec2<f32>(in.bounds.x + in.bounds.z, in.bounds.y); }
    case 2u: { local = vec2<f32>(in.bounds.x, in.bounds.y + in.bounds.w); }
    default: { local = vec2<f32>(in.bounds.x + in.bounds.z, in.bounds.y + in.bounds.w); }
  }

  let world = applyTransform(in.matAB, in.transTZ, local);

  var out : RectVertexOut;
  out.position = vec4<f32>(toNdc(world), in.transTZ.z, 1.0);
  out.color = in.color;
  return out;
}

@fragment
fn fs_rect(in : RectVertexOut) -> @location(0) vec4<f32> {
  return in.color;
}

// ---------------------------------------------------------------------------
// Triangle (port of Triangle.vert / Triangle.frag)
// ---------------------------------------------------------------------------

struct TriVertexIn {
  @builtin(vertex_index) vertexIndex : u32,
  @location(0) matAB : vec4<f32>,
  @location(1) transTZ : vec4<f32>,
  @location(2) p12 : vec4<f32>, // (x1, y1, x2, y2)
  @location(3) p3 : vec4<f32>,  // (x3, y3, unused, unused)
  @location(4) color : vec4<f32>,
};

struct TriVertexOut {
  @builtin(position) position : vec4<f32>,
  @location(0) color : vec4<f32>,
};

@vertex
fn vs_triangle(in : TriVertexIn) -> TriVertexOut {
  var local : vec2<f32>;
  switch (in.vertexIndex) {
    case 0u: { local = in.p12.xy; }
    case 1u: { local = in.p12.zw; }
    default: { local = in.p3.xy; }
  }

  let world = applyTransform(in.matAB, in.transTZ, local);

  var out : TriVertexOut;
  out.position = vec4<f32>(toNdc(world), in.transTZ.z, 1.0);
  out.color = in.color;
  return out;
}

@fragment
fn fs_triangle(in : TriVertexOut) -> @location(0) vec4<f32> {
  return in.color;
}

// ---------------------------------------------------------------------------
// Ellipse (port of Ellipse.vert / Ellipse.frag) — SDF-ish unit-circle test, discard outside.
// ---------------------------------------------------------------------------

struct EllipseVertexIn {
  @builtin(vertex_index) vertexIndex : u32,
  @location(0) matAB : vec4<f32>,
  @location(1) transTZ : vec4<f32>,
  @location(2) bounds : vec4<f32>, // (cx, cy, width, height)
  @location(3) color : vec4<f32>,
};

struct EllipseVertexOut {
  @builtin(position) position : vec4<f32>,
  @location(0) fragPos : vec2<f32>,
  @location(1) bounds : vec4<f32>,
  @location(2) color : vec4<f32>,
};

@vertex
fn vs_ellipse(in : EllipseVertexIn) -> EllipseVertexOut {
  let halfW = in.bounds.z * 0.5;
  let halfH = in.bounds.w * 0.5;

  var local : vec2<f32>;
  switch (in.vertexIndex) {
    case 0u: { local = vec2<f32>(in.bounds.x - halfW, in.bounds.y - halfH); }
    case 1u: { local = vec2<f32>(in.bounds.x + halfW, in.bounds.y - halfH); }
    case 2u: { local = vec2<f32>(in.bounds.x - halfW, in.bounds.y + halfH); }
    default: { local = vec2<f32>(in.bounds.x + halfW, in.bounds.y + halfH); }
  }

  let world = applyTransform(in.matAB, in.transTZ, local);

  var out : EllipseVertexOut;
  out.position = vec4<f32>(toNdc(world), in.transTZ.z, 1.0);
  out.fragPos = world;
  out.bounds = in.bounds;
  out.color = in.color;
  return out;
}

@fragment
fn fs_ellipse(in : EllipseVertexOut) -> @location(0) vec4<f32> {
  let offsetFromCenter = in.fragPos - in.bounds.xy;
  let ellipseScale = vec2<f32>(2.0 / in.bounds.z, 2.0 / in.bounds.w);
  let normalizedPos = offsetFromCenter * ellipseScale;

  let isInside = select(0.0, 1.0, dot(normalizedPos, normalizedPos) <= 1.0);

  return vec4<f32>(in.color.rgb, in.color.a * isInside);
}
