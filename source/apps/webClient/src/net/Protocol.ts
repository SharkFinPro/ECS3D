// Port of source/libs/protocol/Protocol.h - the wire format.
//
// Message is an append-only byte vector fed by std::bit_cast, so the wire is tightly packed
// little-endian with no alignment padding anywhere. Every read/write below mirrors one C++ type:
//
//   MessageType / Role    1 byte  (explicitly `: uint8_t`)
//   ComponentType /       4 bytes (unsized `enum class` -> int)
//   AssetType / SceneStatus
//   bool                  1 byte
//   float / int32 / uint32 4 bytes
//   size_t                8 bytes
//   glm::vec3            12 bytes, no padding
//   uuid (string form)   uint32 length + 36 ASCII chars
//   uuid (raw form)      16 bytes  (ModelRenderer's model/texture/specular only)
//
// There is no length prefix or header on the wire: the WebSocket transport frames each message and
// puts the MessageType in byte 0, payload in the rest (see WebSocketBackend.cs BuildMessage).

export const defaultPort = 3000;

export enum MessageType {
  undefined = 0,
  join = 1,
  snapshot = 2,
  stateDelta = 3,
  inputState = 4,
  editComponent = 5,
  sceneEdit = 6,
  sceneControl = 7,
  loadProject = 8,
  addAsset = 9,
  editStatus = 10,
  sceneStatus = 11,
  objectSpawned = 12,
  objectDestroyed = 13,
  playerSlot = 14,
  renameAsset = 15,
  removeAsset = 16,
}

export enum Role {
  player = 0,
  editor = 1,
}

export type Vec3 = [number, number, number];

// Grows geometrically like std::vector, so a snapshot-sized payload doesn't reallocate per write.
export class Message {
  private buffer: ArrayBuffer;
  private view: DataView;
  private length = 0;

  constructor(readonly type: MessageType = MessageType.undefined) {
    this.buffer = new ArrayBuffer(256);
    this.view = new DataView(this.buffer);
  }

  get size(): number {
    return this.length;
  }

  bytes(): Uint8Array {
    return new Uint8Array(this.buffer, 0, this.length);
  }

  writeUint8(value: number): this {
    this.reserve(1);
    this.view.setUint8(this.length, value);
    this.length += 1;
    return this;
  }

  writeBool(value: boolean): this {
    return this.writeUint8(value ? 1 : 0);
  }

  writeInt32(value: number): this {
    this.reserve(4);
    this.view.setInt32(this.length, value, true);
    this.length += 4;
    return this;
  }

  writeUint32(value: number): this {
    this.reserve(4);
    this.view.setUint32(this.length, value, true);
    this.length += 4;
    return this;
  }

  // std::size_t is 8 bytes on every platform the server builds for.
  writeSizeT(value: number): this {
    this.reserve(8);
    this.view.setBigUint64(this.length, BigInt(value), true);
    this.length += 8;
    return this;
  }

  writeUint64(value: bigint): this {
    this.reserve(8);
    this.view.setBigUint64(this.length, value, true);
    this.length += 8;
    return this;
  }

  writeFloat(value: number): this {
    this.reserve(4);
    this.view.setFloat32(this.length, value, true);
    this.length += 4;
    return this;
  }

  // Length-prefixed string (uint32 size + bytes), pairing with MessageReader.readString.
  writeString(value: string): this {
    const encoded = new TextEncoder().encode(value);
    this.writeUint32(encoded.length);
    this.reserve(encoded.length);
    new Uint8Array(this.buffer).set(encoded, this.length);
    this.length += encoded.length;
    return this;
  }

  private reserve(extra: number): void {
    if (this.length + extra <= this.buffer.byteLength) {
      return;
    }

    let capacity = this.buffer.byteLength * 2;
    while (capacity < this.length + extra) {
      capacity *= 2;
    }

    const grown = new ArrayBuffer(capacity);
    new Uint8Array(grown).set(new Uint8Array(this.buffer, 0, this.length));
    this.buffer = grown;
    this.view = new DataView(this.buffer);
  }
}

export class MessageReader {
  private readonly view: DataView;
  private offset = 0;

  constructor(private readonly data: Uint8Array) {
    this.view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  }

  get remaining(): number {
    return this.data.byteLength - this.offset;
  }

  readUint8(): number {
    this.require(1);
    const value = this.view.getUint8(this.offset);
    this.offset += 1;
    return value;
  }

  readBool(): boolean {
    return this.readUint8() !== 0;
  }

  readInt32(): number {
    this.require(4);
    const value = this.view.getInt32(this.offset, true);
    this.offset += 4;
    return value;
  }

  readUint32(): number {
    this.require(4);
    const value = this.view.getUint32(this.offset, true);
    this.offset += 4;
    return value;
  }

  readUint64(): bigint {
    this.require(8);
    const value = this.view.getBigUint64(this.offset, true);
    this.offset += 8;
    return value;
  }

  readFloat(): number {
    this.require(4);
    const value = this.view.getFloat32(this.offset, true);
    this.offset += 4;
    return value;
  }

  readVec3(): Vec3 {
    return [this.readFloat(), this.readFloat(), this.readFloat()];
  }

  // An unsized `enum class` (ComponentType, AssetType, SceneStatus) is backed by int.
  readEnum(): number {
    return this.readInt32();
  }

  readString(): string {
    const size = this.readUint32();
    this.require(size);
    const value = new TextDecoder().decode(this.data.subarray(this.offset, this.offset + size));
    this.offset += size;
    return value;
  }

  // A uuid written raw (std::array<uint8_t,16>), rendered in canonical 8-4-4-4-12 form so it keys
  // the same maps as the string-form uuids the rest of the protocol uses.
  readRawUUID(): string {
    this.require(16);
    let hex = "";
    for (let i = 0; i < 16; ++i) {
      hex += this.data[this.offset + i].toString(16).padStart(2, "0");
    }
    this.offset += 16;

    return (
      `${hex.slice(0, 8)}-${hex.slice(8, 12)}-${hex.slice(12, 16)}-` +
      `${hex.slice(16, 20)}-${hex.slice(20, 32)}`
    );
  }

  private require(bytes: number): void {
    if (bytes > this.remaining) {
      throw new Error("Message underflow");
    }
  }
}

// uuids::uuid::is_nil() - the all-zero uuid, which every unset asset slot carries.
export const nilUUID = "00000000-0000-0000-0000-000000000000";

export function isNilUUID(uuid: string): boolean {
  return uuid === nilUUID;
}
