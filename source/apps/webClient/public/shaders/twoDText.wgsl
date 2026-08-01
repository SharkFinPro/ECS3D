// Renderer2D glyph shader — port of source/shaders/2D/Font.{vert,frag}. The original samples
// an FreeType-rendered alpha atlas through a combined sampler2D; here the atlas is rasterized
// by textAtlas.ts (Canvas2D + FontFace, see that file's header) into an r8unorm texture, and
// WGSL requires the texture/sampler split explicitly (see group(1) below).

struct ScreenUniform {
  width : f32,
  height : f32,
  pad0 : f32,
  pad1 : f32,
};

@group(0) @binding(0) var<uniform> screen : ScreenUniform;
@group(1) @binding(0) var glyphAtlas : texture_2d<f32>;
@group(1) @binding(1) var glyphSampler : sampler;

struct GlyphVertexIn {
  @builtin(vertex_index) vertexIndex : u32,
  @location(0) matAB : vec4<f32>,
  @location(1) transTZ : vec4<f32>, // (tx, ty, z, unused)
  @location(2) bounds : vec4<f32>,  // (x, y, width, height) — top-left origin
  @location(3) uvRect : vec4<f32>,  // (u0, v0, u1, v1)
  @location(4) color : vec4<f32>,
};

struct GlyphVertexOut {
  @builtin(position) position : vec4<f32>,
  @location(0) uv : vec2<f32>,
  @location(1) color : vec4<f32>,
};

fn applyTransform(matAB : vec4<f32>, transTZ : vec4<f32>, p : vec2<f32>) -> vec2<f32> {
  return vec2<f32>(
    matAB.x * p.x + matAB.z * p.y + transTZ.x,
    matAB.y * p.x + matAB.w * p.y + transTZ.y,
  );
}

fn toNdc(worldPos : vec2<f32>) -> vec2<f32> {
  return vec2<f32>(
    2.0 * worldPos.x / screen.width - 1.0,
    1.0 - 2.0 * worldPos.y / screen.height,
  );
}

@vertex
fn vs_main(in : GlyphVertexIn) -> GlyphVertexOut {
  var local : vec2<f32>;
  var uvLocal : vec2<f32>;
  switch (in.vertexIndex) {
    case 0u: {
      local = vec2<f32>(in.bounds.x, in.bounds.y);
      uvLocal = vec2<f32>(0.0, 0.0);
    }
    case 1u: {
      local = vec2<f32>(in.bounds.x + in.bounds.z, in.bounds.y);
      uvLocal = vec2<f32>(1.0, 0.0);
    }
    case 2u: {
      local = vec2<f32>(in.bounds.x, in.bounds.y + in.bounds.w);
      uvLocal = vec2<f32>(0.0, 1.0);
    }
    default: {
      local = vec2<f32>(in.bounds.x + in.bounds.z, in.bounds.y + in.bounds.w);
      uvLocal = vec2<f32>(1.0, 1.0);
    }
  }

  let world = applyTransform(in.matAB, in.transTZ, local);

  var out : GlyphVertexOut;
  out.position = vec4<f32>(toNdc(world), in.transTZ.z, 1.0);
  out.uv = mix(in.uvRect.xy, in.uvRect.zw, uvLocal);
  out.color = in.color;
  return out;
}

@fragment
fn fs_main(in : GlyphVertexOut) -> @location(0) vec4<f32> {
  let alpha = textureSample(glyphAtlas, glyphSampler, in.uv).r;
  return vec4<f32>(in.color.rgb, alpha * in.color.a);
}
