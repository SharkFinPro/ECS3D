// Port of source/libs/net/NetClient.{h,cpp} plus the client half of the C# transport
// (net/Transport/WebSocketBackend.cs).
//
// Upstream splits these across the CLR boundary: NetClient owns the protocol in C++ and hands the
// C# backend opaque (type byte, payload) pairs. A browser has the socket natively, so the two halves
// collapse into one class - but the wire is byte-identical, including the handshake frame the C#
// backend consumes before any protocol message.
//
// DEVIATION: ClientApp::connectToServer blocks on a 250ms retry loop for 15s. A browser cannot block,
// so connect() is async and the caller awaits it; the retry cadence and deadline are unchanged.

import { Message, MessageType, Role } from "./Protocol";

// Reserved type byte outside the MessageType enum, carrying [role][token UTF-8]. The server consumes
// it at the transport layer and never delivers it upward (TransportBackend.cs HandshakeType).
const handshakeType = 0xff;

const connectDeadlineMs = 15_000;
const retryDelayMs = 250;

// Inbox cap, in messages. A hidden tab stops requestAnimationFrame, so frame() (and with it the
// inbox drain) stops while the socket keeps delivering ~50 stateDeltas a second - roughly 40KB/s of
// growth for as long as the tab stays backgrounded. The C++ client cannot hit this: its loop always
// runs. See dropSupersededDelta for why trimming is lossless.
const maxInboxMessages = 256;

export interface InboundMessage {
  type: MessageType;
  payload: Uint8Array;
}

export class NetClient {
  private socket: WebSocket | null = null;
  private readonly inbox: InboundMessage[] = [];
  private connected = false;

  // Set by disconnect(), and never cleared: a NetClient is single-use, like the one-connection-per-
  // process NetClient upstream. Without it an in-flight connect() retry loop outlives the disconnect
  // and opens a socket nobody drains - which in React StrictMode (mount, unmount, remount) means the
  // discarded first client silently holds a server player slot for the rest of the session.
  private closed = false;

  isConnected(): boolean {
    return this.connected;
  }

  // Retries like ClientApp::connectToServer: a just-launched server needs a moment before it listens,
  // so a refused attempt is not fatal until the deadline passes.
  async connect(host: string, port: number, role: Role, authToken: string): Promise<boolean> {
    if (this.connected) {
      return true;
    }

    const deadline = Date.now() + connectDeadlineMs;

    do {
      if (this.closed) {
        return false;
      }

      if (await this.tryConnect(host, port, role, authToken)) {
        return true;
      }

      await new Promise((resolve) => setTimeout(resolve, retryDelayMs));
    } while (Date.now() < deadline);

    if (!this.closed) {
      console.error(`[Client] Could not connect to ${host}:${port}.`);
    }

    return false;
  }

  private tryConnect(host: string, port: number, role: Role, authToken: string): Promise<boolean> {
    return new Promise((resolve) => {
      let socket: WebSocket;
      try {
        socket = new WebSocket(`ws://${host}:${port}/`);
      } catch {
        resolve(false);
        return;
      }

      socket.binaryType = "arraybuffer";

      // Guard against resolving twice - a failed socket fires both error and close.
      let settled = false;
      const settle = (ok: boolean) => {
        if (settled) {
          return;
        }
        settled = true;
        resolve(ok);
      };

      socket.onopen = () => {
        // disconnect() can land while this socket is still CONNECTING; adopting it here would
        // resurrect a client that is already gone.
        if (this.closed) {
          socket.close();
          settle(false);
          return;
        }

        this.socket = socket;
        this.connected = true;

        // Role + token first, so the server can authorize this connection before any protocol
        // message. A player is always admitted; the token only gates Role.editor.
        this.sendHandshake(role, authToken);
        settle(true);
      };

      socket.onmessage = (event) => this.enqueue(new Uint8Array(event.data as ArrayBuffer));

      socket.onerror = () => {
        socket.close();
        settle(false);
      };

      socket.onclose = () => {
        if (this.socket === socket) {
          this.socket = null;
          this.connected = false;
        }
        settle(false);
      };
    });
  }

  disconnect(): void {
    // Set before the guard, not after: the common case for a discarded client is disconnecting while
    // it is still CONNECTING, where `connected` is false and an early return would leave the retry
    // loop running.
    this.closed = true;

    if (!this.connected) {
      return;
    }

    this.connected = false;
    this.socket?.close();
    this.socket = null;
  }

  send(message: Message): void {
    if (!this.connected || !this.socket) {
      return;
    }

    const payload = message.bytes();
    const frame = new Uint8Array(1 + payload.length);
    frame[0] = message.type;
    frame.set(payload, 1);

    this.socket.send(frame);
  }

  // Drains one message, mirroring NetClient::poll's MessageQueue pop. Returns null when empty.
  poll(): InboundMessage | null {
    return this.inbox.shift() ?? null;
  }

  // Takes the whole backlog at once, keeping only the NEWEST stateDelta. Each delta restates every
  // object's transform, so applying an older one just to overwrite it a moment later is pure waste -
  // and it is self-reinforcing: a slow frame lets deltas pile up, and draining all of them makes the
  // next frame slower still. Dropping the stale ones is safe whatever they interleave with, because
  // the surviving delta is applied last and covers every object the snapshot/spawn/destroy left behind.
  //
  // Order is otherwise preserved: snapshots, component edits, spawns and destroys are stateful and all
  // survive, in sequence.
  drain(): InboundMessage[] {
    if (this.inbox.length === 0) {
      return [];
    }

    const batch = this.inbox.splice(0, this.inbox.length);

    let lastDelta = -1;
    for (let i = batch.length - 1; i >= 0; --i) {
      if (batch[i].type === MessageType.stateDelta) {
        lastDelta = i;
        break;
      }
    }

    if (lastDelta < 0) {
      return batch;
    }

    return batch.filter((message, i) => message.type !== MessageType.stateDelta || i === lastDelta);
  }

  private sendHandshake(role: Role, token: string): void {
    const encodedToken = new TextEncoder().encode(token);

    const frame = new Uint8Array(2 + encodedToken.length);
    frame[0] = handshakeType;
    frame[1] = role;
    frame.set(encodedToken, 2);

    this.socket?.send(frame);
  }

  private enqueue(frame: Uint8Array): void {
    if (frame.length < 1) {
      return;
    }

    this.inbox.push({ type: frame[0] as MessageType, payload: frame.subarray(1) });

    while (this.inbox.length > maxInboxMessages && this.dropSupersededDelta()) {
      // Trim until the backlog fits or there is nothing safe left to drop.
    }
  }

  // Drops the oldest queued stateDelta. This is lossless: packStateDelta writes EVERY object's local
  // transform every tick, so a delta is a full restatement rather than an increment, and applying
  // only the newest lands on the same state. Every other message type is stateful - a snapshot, a
  // component edit, a spawn or a destroy cannot be skipped - so those are never dropped and the queue
  // can still grow if one somehow dominates. Returns false when no delta remains to drop.
  private dropSupersededDelta(): boolean {
    const index = this.inbox.findIndex((message) => message.type === MessageType.stateDelta);

    if (index < 0) {
      return false;
    }

    this.inbox.splice(index, 1);
    return true;
  }
}
