// Port of source/libs/data/objects/components/ModelRenderer.{h,cpp}.
//
// The three asset uuids are packed RAW (16 bytes each), unlike the object/scene uuids that travel as
// length-prefixed strings - readRawUUID renders them into the same canonical form so both key the
// AssetRegistry identically.

import type { MessageReader } from "../../../net/Protocol";
import { isNilUUID, nilUUID } from "../../../net/Protocol";
import { Component, ComponentType } from "./Component";

export class ModelRenderer extends Component {
  private shouldRender = false;
  private useStandardPipeline = true;
  private reflectivity = 0;

  private modelUUID = nilUUID;
  private textureUUID = nilUUID;
  private specularMapUUID = nilUUID;

  constructor() {
    super(ComponentType.modelRenderer);
  }

  getShouldRender(): boolean {
    return this.shouldRender;
  }

  getUseStandardPipeline(): boolean {
    return this.useStandardPipeline;
  }

  getReflectivity(): number {
    return this.reflectivity;
  }

  getModelUUID(): string {
    return this.modelUUID;
  }

  getTextureUUID(): string {
    return this.textureUUID;
  }

  getSpecularMapUUID(): string {
    return this.specularMapUUID;
  }

  canRender(): boolean {
    return (
      !isNilUUID(this.modelUUID) &&
      !isNilUUID(this.textureUUID) &&
      !isNilUUID(this.specularMapUUID)
    );
  }

  unpack(reader: MessageReader): void {
    this.shouldRender = reader.readBool();
    this.useStandardPipeline = reader.readBool();
    this.reflectivity = reader.readFloat();

    this.modelUUID = reader.readRawUUID();
    this.textureUUID = reader.readRawUUID();
    this.specularMapUUID = reader.readRawUUID();
  }

  loadFromJSON(data: Record<string, unknown>): void {
    this.shouldRender = Boolean(data.shouldRender);
    this.useStandardPipeline = data.useStandardPipeline === undefined
      ? true
      : Boolean(data.useStandardPipeline);
    this.reflectivity = Number(data.reflectivity ?? 0);

    // An unset slot serializes as "", which uuid::from_string rejects - upstream then leaves the
    // existing value alone rather than clearing it.
    this.modelUUID = parseUUID(data.modelUUID) ?? this.modelUUID;
    this.textureUUID = parseUUID(data.textureUUID) ?? this.textureUUID;
    this.specularMapUUID = parseUUID(data.specularMapUUID) ?? this.specularMapUUID;
  }
}

const uuidPattern = /^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$/;

function parseUUID(value: unknown): string | null {
  if (typeof value !== "string" || !uuidPattern.test(value)) {
    return null;
  }

  return value.toLowerCase();
}
