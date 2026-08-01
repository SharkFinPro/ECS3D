// Port of source/components/assets/objects/RenderObject.{h,cpp} — a drawable instance: a Model,
// its diffuse + specular textures bound as one descriptor set, and a transform uniform.
//
// DEVIATIONS:
//  - Orientation is stored as Euler degrees only. Upstream also exposes a quaternion setter, but
//    nothing in the ported tests uses it and Math.ts has no quaternion type.
//  - reflectivity / refractivity / indexOfRefraction are omitted: they only feed the ray tracer,
//    which is not ported (WebGPUPortGuide.md §1).
//  - The transform uniform holds model + normal matrices rather than upstream's model/view/proj,
//    because view and projection live in the per-frame camera uniform shared by every pipeline.

import { Mat4, Vec3, composeTransform, mat4Invert, mat4Transpose } from "../../../utilities/Math";
import { UniformBuffer } from "../../pipelines/uniformBuffers/UniformBuffer";
import { Texture } from "../textures/Texture";
import { Model } from "./Model";

export class RenderObject {
  private position: Vec3 = [0, 0, 0];
  private orientationEuler: Vec3 = [0, 0, 0]; // degrees
  private scale: Vec3 = [1, 1, 1];

  private dirty = false;

  private readonly transformUniform: UniformBuffer;
  private readonly descriptorSet: GPUBindGroup;

  constructor(
    private readonly device: GPUDevice,
    private readonly model: Model,
    private readonly texture: Texture,
    private readonly specularMap: Texture,
    descriptorSetLayout: GPUBindGroupLayout,
  ) {
    this.transformUniform = UniformBuffer.create(device, "vke.ObjectUniform", 128);

    this.descriptorSet = device.createBindGroup({
      label: "vke.RenderObject.DescriptorSet",
      layout: descriptorSetLayout,
      entries: [
        { binding: 0, resource: texture.getSampler() },
        { binding: 1, resource: texture.getImageView() },
        { binding: 2, resource: specularMap.getImageView() },
      ],
    });

    this.updateUniformBuffer();
  }

  // ECS3D CHANGE: the setters mark dirty instead of uploading. Each upload recomposes the model
  // matrix, inverts and transposes it for the normal matrix, and calls writeBuffer - and the render
  // system sets all three every frame, so a 541-object scene paid 1623 uploads and 1623 matrix
  // inversions per frame, three quarters of them redundant. flushTransform() does the work once, at
  // submission, and the equality check skips it entirely for objects that did not move (most of them
  // in a large scene).
  setPosition(position: Vec3): void {
    if (equalVec3(this.position, position)) {
      return;
    }

    this.position = [...position] as Vec3;
    this.dirty = true;
  }

  setScale(scale: Vec3 | number): void {
    const next: Vec3 = typeof scale === "number" ? [scale, scale, scale] : ([...scale] as Vec3);

    if (equalVec3(this.scale, next)) {
      return;
    }

    this.scale = next;
    this.dirty = true;
  }

  setOrientationEuler(orientation: Vec3): void {
    if (equalVec3(this.orientationEuler, orientation)) {
      return;
    }

    this.orientationEuler = [...orientation] as Vec3;
    this.dirty = true;
  }

  // Uploads the transform if it changed since the last flush. Called once per frame per object when
  // Renderer3D queues it, so the shadow pass's repeated draws never re-upload.
  flushTransform(): void {
    if (!this.dirty) {
      return;
    }

    this.dirty = false;
    this.updateUniformBuffer();
  }

  getPosition(): Vec3 {
    return [...this.position] as Vec3;
  }

  getScale(): Vec3 {
    return [...this.scale] as Vec3;
  }

  getOrientationEuler(): Vec3 {
    return [...this.orientationEuler] as Vec3;
  }

  getModel(): Model {
    return this.model;
  }

  getModelMatrix(): Mat4 {
    return composeTransform(this.position, this.orientationEuler, this.scale);
  }

  getTexture(): Texture {
    return this.texture;
  }

  getSpecularMap(): Texture {
    return this.specularMap;
  }

  // The material descriptor set (sampler + diffuse + specular).
  getDescriptorSet(): GPUBindGroup {
    return this.descriptorSet;
  }

  getTransformUniform(): UniformBuffer {
    return this.transformUniform;
  }

  // Binds this object's mesh and issues the indexed draw — RenderObject::draw(commandBuffer).
  draw(pass: GPURenderPassEncoder): void {
    pass.setVertexBuffer(0, this.model.vertexBuffer);
    pass.setIndexBuffer(this.model.indexBuffer, "uint32");
    pass.drawIndexed(this.model.indexCount);
  }

  updateUniformBuffer(): void {
    const modelMatrix = this.getModelMatrix();
    const normalMatrix = mat4Transpose(mat4Invert(modelMatrix));

    const data = new Float32Array(32);
    data.set(modelMatrix, 0);
    data.set(normalMatrix, 16);
    this.transformUniform.update(this.device.queue, data);
  }
}

function equalVec3(a: Vec3, b: Vec3): boolean {
  return a[0] === b[0] && a[1] === b[1] && a[2] === b[2];
}
