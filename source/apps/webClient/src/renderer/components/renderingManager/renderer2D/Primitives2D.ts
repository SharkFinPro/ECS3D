// Port of source/components/renderingManager/renderer2D/Primitives2D.h.
//
// Upstream this header declares one push-constant struct per primitive (Rect::PushConstant,
// Triangle::PushConstant, Ellipse::PushConstant, Glyph::PushConstant), pushed once per draw call.
//
// DEVIATION: WebGPU has no push constants, and a per-primitive uniform write plus a draw call
// each would be far more expensive than the Vulkan push. So the same per-primitive payload rides
// in an INSTANCE vertex buffer instead, and every queued primitive of a kind draws in one
// instanced call. The field order below is exactly the upstream struct order, which is also what
// twoD.wgsl / twoDText.wgsl declare as their vertex inputs.

export const RECT_FLOATS = 16; // matAB(4) + transTZ(4) + bounds(4) + color(4)
export const TRIANGLE_FLOATS = 20; // matAB(4) + transTZ(4) + p12(4) + p3(4) + color(4)
export const GLYPH_FLOATS = 20; // matAB(4) + transTZ(4) + bounds(4) + uvRect(4) + color(4)

// Rect and Ellipse share a payload: a 2D affine matrix, a depth key, a bounds rect and a colour.
export const instanceLayoutRectEllipse: GPUVertexBufferLayout = {
  arrayStride: RECT_FLOATS * 4,
  stepMode: "instance",
  attributes: [
    { shaderLocation: 0, offset: 0, format: "float32x4" }, // matAB
    { shaderLocation: 1, offset: 16, format: "float32x4" }, // transTZ
    { shaderLocation: 2, offset: 32, format: "float32x4" }, // bounds
    { shaderLocation: 3, offset: 48, format: "float32x4" }, // color
  ],
};

export const instanceLayoutTriangle: GPUVertexBufferLayout = {
  arrayStride: TRIANGLE_FLOATS * 4,
  stepMode: "instance",
  attributes: [
    { shaderLocation: 0, offset: 0, format: "float32x4" }, // matAB
    { shaderLocation: 1, offset: 16, format: "float32x4" }, // transTZ
    { shaderLocation: 2, offset: 32, format: "float32x4" }, // p12
    { shaderLocation: 3, offset: 48, format: "float32x4" }, // p3
    { shaderLocation: 4, offset: 64, format: "float32x4" }, // color
  ],
};

export const instanceLayoutGlyph: GPUVertexBufferLayout = {
  arrayStride: GLYPH_FLOATS * 4,
  stepMode: "instance",
  attributes: [
    { shaderLocation: 0, offset: 0, format: "float32x4" }, // matAB
    { shaderLocation: 1, offset: 16, format: "float32x4" }, // transTZ
    { shaderLocation: 2, offset: 32, format: "float32x4" }, // bounds
    { shaderLocation: 3, offset: 48, format: "float32x4" }, // uvRect
    { shaderLocation: 4, offset: 64, format: "float32x4" }, // color
  ],
};
