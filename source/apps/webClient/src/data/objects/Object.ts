// Port of source/libs/data/objects/Object.{h,cpp}.
//
// DEVIATION: the class is `GameObject`, not `Object` - the upstream name shadows the JavaScript
// global inside its own module, which would break any Object.keys/assign use in this file. The path
// still mirrors upstream.
//
// Read-only half: unpack() and loadFromJSON() are here (both replication paths land on them), but
// pack()/serialize() are not - this client never sends scene state back. start()/stop() are absent
// for the same reason: they drive ComponentVariable's play state, which is server-side.

import type { MessageReader } from "../../net/Protocol";
import {
  Component,
  ComponentType,
  componentTypeToRegistryKey,
  subComponentTypeToParent,
} from "./components/Component";
import { Script } from "./components/Script";
import type { ObjectManager } from "./ObjectManager";

export class GameObject {
  private uuid = "";
  private name = "";

  private parent: GameObject | null = null;
  private readonly children: GameObject[] = [];

  private readonly components = new Map<ComponentType, Component>();
  private readonly scripts: Script[] = [];

  constructor(private manager: ObjectManager | null = null) {}

  getUUID(): string {
    return this.uuid;
  }

  getName(): string {
    return this.name;
  }

  setManager(manager: ObjectManager): void {
    this.manager = manager;
  }

  getParent(): GameObject | null {
    return this.parent;
  }

  setParent(parent: GameObject | null): void {
    this.parent = parent;
  }

  addChild(child: GameObject): void {
    this.children.push(child);
  }

  removeChild(child: GameObject): void {
    const index = this.children.indexOf(child);
    if (index >= 0) {
      this.children.splice(index, 1);
    }
  }

  getChildren(): readonly GameObject[] {
    return this.children;
  }

  getComponents(): ReadonlyMap<ComponentType, Component> {
    return this.components;
  }

  getScripts(): readonly Script[] {
    return this.scripts;
  }

  // Mirrors Object::getComponent, including the one inheritance rule upstream has: a child with no
  // RigidBody of its own resolves to its parent's.
  getComponent<T extends Component>(type: ComponentType): T | null {
    const component = this.components.get(type);

    if (!component) {
      if (type === ComponentType.rigidBody && this.parent) {
        return this.parent.getComponent<T>(type);
      }

      return null;
    }

    return component as T;
  }

  addComponent(component: Component, setOwner = true): void {
    if (component.type === ComponentType.script) {
      const incoming = component as Script;

      // One script per class name, as upstream.
      for (const script of this.scripts) {
        if (script.getClassName() === incoming.getClassName()) {
          return;
        }
      }

      this.scripts.push(incoming);
    } else {
      this.components.set(component.type, component);
    }

    if (setOwner) {
      component.setOwner(this);
    }
  }

  // Symmetric with Object::pack: rebuilds this object from scratch, creating any missing components,
  // scripts and child objects, so it works on a fresh empty object as well as an existing one.
  unpack(reader: MessageReader): void {
    this.uuid = reader.readString();
    this.name = reader.readString();

    // addObject ran before the uuid was known, so the manager indexes it here.
    this.requireManager().registerUUID(this);

    const registry = this.requireManager().getComponentRegistry();

    const componentCount = reader.readUint32();
    for (let i = 0; i < componentCount; ++i) {
      const packedType = reader.readEnum() as ComponentType;

      // Colliders pack their subtype but live under the parent "collider" key.
      const lookupType = subComponentTypeToParent.get(packedType) ?? packedType;

      // Looked up directly rather than via getComponent, whose rigidBody fallback would otherwise
      // make a fresh object unpack into its parent's component.
      let component = this.components.get(lookupType) ?? null;

      if (!component) {
        const key = componentTypeToRegistryKey.get(packedType);
        component = key ? registry.create(key) : null;

        if (!component) {
          throw new Error(`Unknown component type: ${packedType}`);
        }

        this.addComponent(component);
      }

      component.unpack(reader);
    }

    const scriptCount = reader.readUint32();
    for (let i = 0; i < scriptCount; ++i) {
      reader.readEnum(); // script type tag, not needed for lookup

      const className = reader.readString();

      let script = this.scripts.find((candidate) => candidate.getClassName() === className) ?? null;

      if (!script) {
        script = registry.create("Script") as Script | null;

        if (!script) {
          throw new Error("Script component type is not registered");
        }

        script.setClassName(className);
        this.addComponent(script);
      }

      script.unpack(reader);
    }

    // Children are packed as whole objects; each is wired to this parent and the manager before it
    // unpacks its own subtree, matching upstream's ordering.
    const childCount = reader.readUint32();
    for (let i = 0; i < childCount; ++i) {
      const child = new GameObject();
      child.setParent(this);
      this.requireManager().addObject(child);

      child.unpack(reader);
    }
  }

  loadFromJSON(data: Record<string, unknown>): void {
    this.uuid = String(data.uuid);
    this.name = String(data.name);

    this.requireManager().registerUUID(this);

    const registry = this.requireManager().getComponentRegistry();

    for (const raw of asArray(data.components)) {
      const componentData = raw as Record<string, unknown>;
      const componentType = String(componentData.type);

      // A Collider serializes as type "Collider" with a "subType" (Box/Sphere), and is registered
      // under that subType.
      const registryKey =
        componentType === "Collider" ? String(componentData.subType) : componentType;

      const component = registry.create(registryKey);

      if (!component) {
        throw new Error(`Unknown component type: ${registryKey}`);
      }

      this.addComponent(component);
      component.loadFromJSON(componentData);
    }

    for (const raw of asArray(data.scripts)) {
      const script = registry.create("Script");

      if (!script) {
        throw new Error("Script component type is not registered");
      }

      this.addComponent(script);
      script.loadFromJSON(raw as Record<string, unknown>);
    }
  }

  // Builds this object's children (and their subtrees) from a serialized blob, adding each to the
  // manager - the JSON counterpart of the child loop in unpack().
  loadChildren(childrenData: unknown): void {
    for (const raw of asArray(childrenData)) {
      const childData = raw as Record<string, unknown>;

      const child = new GameObject(this.manager);
      child.setParent(this);
      child.loadFromJSON(childData);

      this.requireManager().addObject(child);

      if (childData.children !== undefined) {
        child.loadChildren(childData.children);
      }
    }
  }

  private requireManager(): ObjectManager {
    if (!this.manager) {
      throw new Error("GameObject has no ObjectManager");
    }

    return this.manager;
  }
}

function asArray(value: unknown): unknown[] {
  return Array.isArray(value) ? value : [];
}
