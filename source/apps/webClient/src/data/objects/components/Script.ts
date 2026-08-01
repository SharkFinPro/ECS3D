// Port of source/libs/data/objects/components/Script.{h,cpp}.
//
// Gameplay scripts run only on the authoritative server (the client links no scripting at all), so
// this is a pure carrier: the class name identifies it, and the fields blob rides through opaquely
// the way it does upstream.
//
// unpack() reads ONLY the fields string - the className that pack() wrote just before it is consumed
// by Object.unpack, which needs it to find or create the right Script instance first.

import type { MessageReader } from "../../../net/Protocol";
import { Component, ComponentType } from "./Component";

export class Script extends Component {
  private className = "";
  private fields: unknown = [];

  constructor() {
    super(ComponentType.script);
  }

  getClassName(): string {
    return this.className;
  }

  setClassName(className: string): void {
    this.className = className;
  }

  getFields(): unknown {
    return this.fields;
  }

  unpack(reader: MessageReader): void {
    const fields = reader.readString();

    try {
      this.fields = JSON.parse(fields);
    } catch {
      // A malformed blob must not abort the whole snapshot; the field values are display-only here.
      this.fields = [];
    }
  }

  loadFromJSON(data: Record<string, unknown>): void {
    if (data.className !== undefined) {
      this.className = String(data.className);
    }

    if (data.fields !== undefined) {
      this.fields = data.fields;
    }
  }
}
