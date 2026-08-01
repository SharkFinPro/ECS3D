// Port of source/components/pipelines/implementations/vertexInputs/Vertex.h.
//
// Same three attributes at the same locations (0 = pos, 1 = normal, 2 = texCoord) and the same
// three views of them, so a pipeline can consume position-only (shadow, highlight, picking) or
// position+normal (crosses) without a second vertex buffer.
//
// DEVIATION: the C++ struct is explicitly padded to 48 bytes (vec3 + float, vec3 + float,
// vec2 + vec2) to satisfy std140-ish alignment when it is also read as a storage buffer. WGSL's
// layout rules let the interleaved vertex stay tightly packed, so the stride here is 32 bytes.
// Model.ts and every procedural mesh builder write this packed layout.

export const VERTEX_STRIDE = 8 * 4; // pos(3) + normal(3) + texCoord(2), all f32

export class Vertex {
  static getBindingDescription(): Omit<GPUVertexBufferLayout, "attributes"> {
    return {
      arrayStride: VERTEX_STRIDE,
      stepMode: "vertex",
    };
  }

  static getAttributeDescriptions(): GPUVertexAttribute[] {
    return [
      { shaderLocation: 0, offset: 0, format: "float32x3" },
      { shaderLocation: 1, offset: 12, format: "float32x3" },
      { shaderLocation: 2, offset: 24, format: "float32x2" },
    ];
  }

  static getAttributeDescriptionsPositionOnly(): GPUVertexAttribute[] {
    return [{ shaderLocation: 0, offset: 0, format: "float32x3" }];
  }

  static getAttributeDescriptionsPositionAndNormal(): GPUVertexAttribute[] {
    return [
      { shaderLocation: 0, offset: 0, format: "float32x3" },
      { shaderLocation: 1, offset: 12, format: "float32x3" },
    ];
  }
}
