// Port of source/libs/data/ComponentRegistry.{h,cpp} + ComponentRegistration.cpp.
//
// The type-name -> factory table deserialization uses, so no layer has to name concrete component
// types across a boundary. Colliders register under their subType key ("Box"/"Sphere"), matching
// Object::loadFromJSON's registry lookup.

import { Component } from "./objects/components/Component";
import { Camera } from "./objects/components/Camera";
import { LightRenderer } from "./objects/components/LightRenderer";
import { ModelRenderer } from "./objects/components/ModelRenderer";
import { PlayerController } from "./objects/components/PlayerController";
import { RigidBody } from "./objects/components/RigidBody";
import { Script } from "./objects/components/Script";
import { Transform } from "./objects/components/Transform";
import { BoxCollider } from "./objects/components/collisions/BoxCollider";
import { SphereCollider } from "./objects/components/collisions/SphereCollider";

export type ComponentFactory = () => Component;

export class ComponentRegistry {
  private readonly factories = new Map<string, ComponentFactory>();

  register(key: string, factory: ComponentFactory): void {
    this.factories.set(key, factory);
  }

  create(key: string): Component | null {
    return this.factories.get(key)?.() ?? null;
  }
}

export function registerDataComponents(registry: ComponentRegistry): void {
  registry.register("Transform", () => new Transform());
  registry.register("ModelRenderer", () => new ModelRenderer());
  registry.register("RigidBody", () => new RigidBody());
  registry.register("LightRenderer", () => new LightRenderer());
  registry.register("Box", () => new BoxCollider());
  registry.register("Sphere", () => new SphereCollider());
  registry.register("Script", () => new Script());
  registry.register("PlayerController", () => new PlayerController());
  registry.register("Camera", () => new Camera());
}
