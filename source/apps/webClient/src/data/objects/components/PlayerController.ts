// Port of source/libs/data/objects/components/PlayerController.{h,cpp}.
//
// The hook that answers "whose view is this?": the client renders through the Camera on the object
// whose playerSlot matches the slot the server bound to this connection (see ClientApp).

import type { MessageReader } from "../../../net/Protocol";
import { Component, ComponentType } from "./Component";

export class PlayerController extends Component {
  private playerSlot = 0;

  constructor() {
    super(ComponentType.playerController);
  }

  getPlayerSlot(): number {
    return this.playerSlot;
  }

  unpack(reader: MessageReader): void {
    this.playerSlot = reader.readInt32();
  }

  loadFromJSON(data: Record<string, unknown>): void {
    this.playerSlot = Number(data.playerSlot ?? 0);
  }
}
