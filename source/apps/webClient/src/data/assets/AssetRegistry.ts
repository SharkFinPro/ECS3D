// Port of source/libs/data/assets/AssetRegistry.{h,cpp}.
//
// uuid -> asset metadata. Carries no bytes: a Model/Texture record names a path the CLIENT resolves
// against its own asset root (public/assets/... here), which is why the browser must ship a copy of
// the server's defaultAssets. The server never sends asset content - ROADMAP.md B3 tracks that.
//
// Prefab bodies are replicated (the server needs them to instantiate) but unused here: instantiation
// is a server-side structural edit, and this client only ever sees the resulting objects.

import type { MessageReader } from "../../net/Protocol";

// Packed raw over the wire as a 4-byte int, so this must stay append-only.
export enum AssetType {
  Unknown = 0,
  Model = 1,
  Texture = 2,
  Scene = 3,
  Script = 4,
  Prefab = 5,
}

export interface AssetRecord {
  uuid: string;
  type: AssetType;
  path: string; // scenes and prefabs store their display name here instead
  className: string; // scripts only
  body: string; // prefabs only: a serialized object blob
  displayName: string; // optional rename override; the path (registry key) never changes
}

export class AssetRegistry {
  private assets = new Map<string, AssetRecord>();
  private loadedPaths = new Map<string, string>();
  private version = 0;

  // First-wins by path. The one exception is a Prefab, whose body updates in place while keeping the
  // original uuid, so anything already holding that uuid keeps working.
  registerAsset(record: AssetRecord): void {
    const existingUUID = record.path ? this.loadedPaths.get(record.path) : undefined;

    if (record.path && existingUUID !== undefined) {
      const existing = this.assets.get(existingUUID);

      if (record.type === AssetType.Prefab && existing?.type === AssetType.Prefab) {
        existing.body = record.body;
        ++this.version;
      }

      return;
    }

    if (!this.assets.has(record.uuid)) {
      this.assets.set(record.uuid, record);
    }

    if (record.path && !this.loadedPaths.has(record.path)) {
      this.loadedPaths.set(record.path, record.uuid);
    }

    ++this.version;
  }

  renameAsset(uuid: string, displayName: string): void {
    const record = this.assets.get(uuid);
    if (!record) {
      return;
    }

    record.displayName = displayName;
    ++this.version;
  }

  removeAsset(uuid: string): void {
    const record = this.assets.get(uuid);
    if (!record) {
      return;
    }

    // Guarded: a first-wins collision may have left the path key owned by a different record.
    if (this.loadedPaths.get(record.path) === uuid) {
      this.loadedPaths.delete(record.path);
    }

    this.assets.delete(uuid);
    ++this.version;
  }

  clear(): void {
    this.assets.clear();
    this.loadedPaths.clear();
    ++this.version;
  }

  // Null-tolerant by design: deleting an asset lets references dangle, and every lookup site is
  // expected to skip rather than throw.
  getByUUID(uuid: string): AssetRecord | null {
    return this.assets.get(uuid) ?? null;
  }

  getByPath(path: string): AssetRecord | null {
    const uuid = this.loadedPaths.get(path);
    return uuid === undefined ? null : (this.assets.get(uuid) ?? null);
  }

  getAssets(): ReadonlyMap<string, AssetRecord> {
    return this.assets;
  }

  getVersion(): number {
    return this.version;
  }

  // Takes over another registry's contents wholesale - the commit half of ProjectPacker's atomic
  // snapshot swap (upstream this is `*m_assetRegistry = std::move(parsedAssets)`).
  adopt(other: AssetRegistry): void {
    this.assets = other.assets;
    this.loadedPaths = other.loadedPaths;
    ++this.version;
  }

  unpack(reader: MessageReader): void {
    const count = reader.readUint32();

    for (let i = 0; i < count; ++i) {
      const type = reader.readEnum() as AssetType;
      const uuid = reader.readString();
      const path = reader.readString();
      const className = reader.readString();
      const body = reader.readString();
      const displayName = reader.readString();

      this.registerAsset({ uuid, type, path, className, body, displayName });
    }
  }
}
