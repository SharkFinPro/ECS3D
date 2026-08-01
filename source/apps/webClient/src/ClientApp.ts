// Port of source/apps/client/ClientApp.{h,cpp} - the lightweight view.
//
// It renders and sends input; it never simulates. Snapshot/stateDelta/spawn/destroy are applied
// verbatim into a replicated scene, exactly as upstream: no prediction, no reconciliation, no
// interpolation (ROADMAP.md E2/E3 park both).
//
// DEVIATIONS, all forced by the browser:
//  - Construction is async (WebGPUEngine.create and the WebSocket connect both are), so the ctor
//    splits into create().
//  - There is no singleplayer path. ClientApp::connectToServer spawns a child ECS3DServer for
//    --launchLocalServer; a page cannot start a process, so a host is always required.
//  - run()'s `while (isActive())` loop inverts into frame(), driven by requestAnimationFrame.

import { ComponentRegistry, registerDataComponents } from "./data/ComponentRegistry";
import { ProjectPacker } from "./data/ProjectPacker";
import { AssetRegistry } from "./data/assets/AssetRegistry";
import { SceneManager, SceneStatus } from "./data/scenes/SceneManager";
import { ComponentType } from "./data/objects/components/Component";
import { Camera } from "./data/objects/components/Camera";
import { PlayerController } from "./data/objects/components/PlayerController";
import {
  applyComponentEdit,
  applyObjectDestroyed,
  applyObjectSpawned,
  unpackStateDelta,
} from "./data/Replication";
import { NetClient, type InboundMessage } from "./net/NetClient";
import { Message, MessageReader, MessageType, Role } from "./net/Protocol";
import { GpuAssetCache } from "./render/GpuAssetCache";
import { RenderSystem } from "./render/RenderSystem";
import { capture, type InputSnapshot } from "./render/InputCapture";
import { WebGPUEngine } from "./renderer/WebGPUEngine";

export interface ConnectOptions {
  host: string;
  port: number;
}

export type ConnectionState = "connecting" | "connected" | "failed";

export interface ClientStatus {
  connection: ConnectionState;
  sceneName: string | null;
  objectCount: number;
  playerSlot: number;
  editable: boolean;
  sceneStatus: SceneStatus;
  freecam: boolean;
  fps: number;
  // True when we hold a slot but no object carries a PlayerController for it, so the view has fallen
  // back to another camera and our input reaches nothing. Worth surfacing: it looks like a bug.
  missingPlayerObject: boolean;
}

export class ClientApp {
  private readonly componentRegistry = new ComponentRegistry();
  private readonly assetRegistry = new AssetRegistry();
  private readonly sceneManager = new SceneManager();
  private readonly projectPacker: ProjectPacker;
  private readonly renderSystem = new RenderSystem();

  private readonly assetCache: GpuAssetCache;

  // Only resend input when it changes, so an idle client doesn't clobber the shared server-side
  // InputState every frame.
  private lastInputKeys: number[] = [];
  private lastInputFocused = false;
  private lastButtons = 0;
  private lastMouseX = 0;
  private lastMouseY = 0;
  private inputSent = false;

  // A per-session random tag sent in join; the server echoes it back in a playerSlot message so this
  // client can pick out its own slot assignment from the broadcast.
  private readonly joinNonce: bigint;
  private playerSlot = -1;

  private editable = false;
  private connection: ConnectionState = "connecting";

  // Detaches the view from the player object and hands it to the built-in free-fly camera. This has
  // no counterpart in ECS3DClient, which always renders through its own player camera; the closest
  // precedent is the editor's View combo, which picks between free-fly and a scene camera.
  private freecam = false;

  // Rolling window of recent frame durations, averaged for the readout. A window rather than an EMA so
  // a single long frame (an asset upload, a snapshot rebuild) shows up and then ages out cleanly.
  private readonly frameDurations: number[] = [];
  private lastFrameTime = 0;

  private constructor(
    private readonly renderer: WebGPUEngine,
    private readonly netClient: NetClient,
  ) {
    registerDataComponents(this.componentRegistry);

    this.projectPacker = new ProjectPacker(
      this.assetRegistry,
      this.sceneManager,
      this.componentRegistry,
    );

    this.assetCache = new GpuAssetCache(this.renderer, this.assetRegistry);

    this.joinNonce = randomNonce();
  }

  static async create(canvas: HTMLCanvasElement): Promise<ClientApp> {
    // The client is a lightweight view, so no custom ImGui style - matching ClientApp::createRenderer.
    const renderer = await WebGPUEngine.create(canvas, {
      window: { width: 1280, height: 720, title: "ECS3D Client" },
      camera: { position: [0, 5, -50] },
      imGui: { useDockspace: false },
    });

    return new ClientApp(renderer, new NetClient());
  }

  // Separate from create() on purpose. The server hands each connection the LOWEST FREE player slot
  // (ServerApp::assignPlayerSlot), so an extra connection - however short-lived - shifts every later
  // client up a slot. React StrictMode mounts, unmounts and remounts in development, so connecting
  // inside create() opened a socket for the discarded first mount too, and the surviving client ended
  // up one slot too high: its camera fell back to another player's, and its input went to a slot no
  // PlayerController reads. Only the mount that survives the cancelled check calls this.
  //
  // Deliberately not awaited: ClientApp::createRenderer runs before connectToServer, so the C++ window
  // is up and drawing throughout the 15s connect retry. Awaiting would leave the canvas blank instead.
  connect(options: ConnectOptions): void {
    void this.connectToServer(options);
  }

  getStatus(): ClientStatus {
    const scene = this.sceneManager.getCurrentScene();

    return {
      // A socket that drops after a successful join reads as failed, not connected.
      connection:
        this.connection === "connected" && !this.netClient.isConnected()
          ? "failed"
          : this.connection,
      sceneName: scene?.getName() ?? null,
      objectCount: scene?.getObjectManager().getAllObjects().length ?? 0,
      playerSlot: this.playerSlot,
      editable: this.editable,
      sceneStatus: this.sceneManager.getSceneStatus(),
      freecam: this.freecam,
      fps: this.getFps(),
      missingPlayerObject: this.playerSlot >= 0 && this.resolvePlayerCamera() === null,
    };
  }

  getFps(): number {
    if (this.frameDurations.length === 0) {
      return 0;
    }

    const total = this.frameDurations.reduce((sum, duration) => sum + duration, 0);

    return Math.round(1000 / (total / this.frameDurations.length));
  }

  isFreecam(): boolean {
    return this.freecam;
  }

  // The free-fly camera keeps its own pose across toggles, so leaving and re-entering freecam returns
  // you to where you left it rather than to the player. Matches RenderSystem.useFreeFlyCamera, which
  // pushes the camera's stored pose on the handover.
  setFreecam(enabled: boolean): void {
    this.freecam = enabled;
  }

  // One iteration of ClientApp::run's loop body.
  frame(timeMs: number): void {
    this.recordFrameTime(timeMs);

    // drain() rather than poll-until-empty: it discards superseded stateDeltas, so a backlog costs one
    // apply instead of one per queued tick.
    for (const message of this.netClient.drain()) {
      this.applyMessage(message);
    }

    this.sendInput();

    this.variableUpdate(timeMs);
  }

  dispose(): void {
    this.netClient.disconnect();
    this.renderer.dispose();
  }

  private recordFrameTime(timeMs: number): void {
    const previous = this.lastFrameTime;
    this.lastFrameTime = timeMs;

    // Skip the first frame (no previous sample) and any gap left by a backgrounded tab, where
    // requestAnimationFrame stops entirely and would otherwise report a multi-second "frame".
    const duration = timeMs - previous;
    if (previous === 0 || duration <= 0 || duration > 1000) {
      return;
    }

    this.frameDurations.push(duration);
    if (this.frameDurations.length > 60) {
      this.frameDurations.shift();
    }
  }

  private async connectToServer(options: ConnectOptions): Promise<void> {
    await this.netClient.connect(options.host, options.port, Role.player, "");

    if (!this.netClient.isConnected()) {
      this.connection = "failed";
      return;
    }

    this.connection = "connected";

    // Ask the server for the initial snapshot, tagged with our nonce so the reply's slot is
    // identifiable among the broadcast.
    const message = new Message(MessageType.join);
    message.writeUint64(this.joinNonce);
    this.netClient.send(message);
  }

  private sendInput(): void {
    const captured = capture(this.renderer.getWindow());

    // In freecam the right-drag and the wheel belong to the local free-fly camera, so the mouse block
    // is zeroed rather than forwarded - otherwise looking around would also turn the player. This is
    // EditorApp::sendInput's rule, which forwards the mouse only while the viewport looks through a
    // scene camera. The keyboard still goes through from either view, as it does there.
    const snapshot = this.freecam ? withoutMouse(captured) : captured;

    // Discrete state we de-dup on. Cursor position is included so mouse movement triggers a send;
    // the delta rides along in the same message.
    const discreteChanged =
      !this.inputSent ||
      !sameKeys(snapshot.keys, this.lastInputKeys) ||
      snapshot.focused !== this.lastInputFocused ||
      snapshot.buttons !== this.lastButtons ||
      snapshot.mouseX !== this.lastMouseX ||
      snapshot.mouseY !== this.lastMouseY;

    // Scroll is a per-frame amount with no resting position, so it must be sent on any frame it is
    // non-zero (the position comparison above doesn't capture it).
    const scrolled = snapshot.scrollY !== 0;

    if (this.inputSent && !discreteChanged && !scrolled) {
      return;
    }

    this.lastInputKeys = snapshot.keys;
    this.lastInputFocused = snapshot.focused;
    this.lastButtons = snapshot.buttons;
    this.lastMouseX = snapshot.mouseX;
    this.lastMouseY = snapshot.mouseY;
    this.inputSent = true;

    this.netClient.send(buildInputState(snapshot));
  }

  private variableUpdate(timeMs: number): void {
    const scene = this.sceneManager.getCurrentScene();

    if (scene) {
      this.renderSystem.variableUpdate(scene.getObjectManager(), this.assetCache);

      if (this.freecam) {
        // Detached: the built-in free-fly camera owns the view and drives itself from local input.
        this.renderSystem.useFreeFlyCamera(this.assetCache);
      } else {
        // Render through this client's own player camera (the object with our PlayerController slot
        // plus a Camera); null falls back to the scene's first active camera / free-fly.
        this.renderSystem.updateCamera(
          scene.getObjectManager(),
          this.assetCache,
          this.resolvePlayerCamera(),
        );
      }
    }

    this.renderer.render(timeMs);
  }

  private applyMessage(message: InboundMessage): void {
    try {
      switch (message.type) {
        case MessageType.snapshot:
          this.handleSnapshot(message.payload);
          break;

        case MessageType.stateDelta:
          this.handleStateDelta(message.payload);
          break;

        case MessageType.editComponent:
          this.handleEditComponent(message.payload);
          break;

        case MessageType.objectSpawned:
          this.handleObjectSpawned(message.payload);
          break;

        case MessageType.objectDestroyed:
          this.handleObjectDestroyed(message.payload);
          break;

        case MessageType.playerSlot:
          this.handlePlayerSlot(message.payload);
          break;

        // Not consumed by the C++ client either; read here only so the status overlay can show them.
        case MessageType.editStatus:
          this.editable = new MessageReader(message.payload).readBool();
          break;

        case MessageType.sceneStatus:
          this.sceneManager.setSceneStatus(new MessageReader(message.payload).readEnum());
          break;

        default:
          break;
      }
    } catch (error) {
      // A malformed message must not kill the render loop. ProjectPacker's parse/commit split
      // already guarantees a bad snapshot leaves the current project intact.
      console.error(`[Client] Failed to apply message type ${message.type}:`, error);
    }
  }

  private handleSnapshot(payload: Uint8Array): void {
    // Full state on join: rebuild the replicated scene from the packed project blob.
    this.projectPacker.unpack(new MessageReader(payload));

    const scene = this.sceneManager.getCurrentScene();
    console.info(
      `[Client] Applied snapshot (${payload.length} bytes). Current scene: ` +
        `${scene ? scene.getName() : "<none>"} ` +
        `(${scene ? scene.getObjectManager().getAllObjects().length : 0} objects).`,
    );
  }

  private handleStateDelta(payload: Uint8Array): void {
    const scene = this.sceneManager.getCurrentScene();

    if (scene) {
      unpackStateDelta(scene.getObjectManager(), payload);
    }
  }

  private handleEditComponent(payload: Uint8Array): void {
    // The server applied an editor's component change; mirror it into the replicated scene.
    const scene = this.sceneManager.getCurrentScene();

    if (scene) {
      applyComponentEdit(scene.getObjectManager(), payload);
    }
  }

  private handleObjectSpawned(payload: Uint8Array): void {
    // A script spawned an object at runtime; splice the packed object into the replicated scene.
    const scene = this.sceneManager.getCurrentScene();

    if (scene) {
      applyObjectSpawned(scene.getObjectManager(), payload);
    }
  }

  private handleObjectDestroyed(payload: Uint8Array): void {
    const scene = this.sceneManager.getCurrentScene();

    if (scene) {
      applyObjectDestroyed(scene.getObjectManager(), payload);
    }
  }

  private handlePlayerSlot(payload: Uint8Array): void {
    // The server broadcasts every client's slot assignment; keep only the one tagged with our nonce.
    const reader = new MessageReader(payload);
    const nonce = reader.readUint64();
    const slot = reader.readInt32();

    if (nonce === this.joinNonce) {
      this.playerSlot = slot;
    }
  }

  // The object this client should render through: the one carrying a PlayerController for our slot
  // and a Camera. Null when the slot is still unknown or no such camera exists.
  private resolvePlayerCamera(): string | null {
    if (this.playerSlot < 0) {
      return null;
    }

    const scene = this.sceneManager.getCurrentScene();
    if (!scene) {
      return null;
    }

    for (const object of scene.getObjectManager().getAllObjects()) {
      const playerController = object.getComponent<PlayerController>(
        ComponentType.playerController,
      );

      if (!playerController || playerController.getPlayerSlot() !== this.playerSlot) {
        continue;
      }

      if (object.getComponent<Camera>(ComponentType.camera)) {
        return object.getUUID();
      }
    }

    return null;
  }
}

// Payload layout per Protocol.h's inputState comment: focused (bool), key count (size_t) + that many
// key codes (int32), then mouseX, mouseY, mouseDeltaX, mouseDeltaY, scrollY (5x float), and the
// button bitmask (uint8).
function buildInputState(snapshot: InputSnapshot): Message {
  const message = new Message(MessageType.inputState);

  message.writeBool(snapshot.focused);
  message.writeSizeT(snapshot.keys.length);
  for (const key of snapshot.keys) {
    message.writeInt32(key);
  }

  message.writeFloat(snapshot.mouseX);
  message.writeFloat(snapshot.mouseY);
  message.writeFloat(snapshot.mouseDeltaX);
  message.writeFloat(snapshot.mouseDeltaY);
  message.writeFloat(snapshot.scrollY);
  message.writeUint8(snapshot.buttons);

  return message;
}

function withoutMouse(snapshot: InputSnapshot): InputSnapshot {
  return {
    ...snapshot,
    mouseX: 0,
    mouseY: 0,
    mouseDeltaX: 0,
    mouseDeltaY: 0,
    scrollY: 0,
    buttons: 0,
  };
}

function sameKeys(a: readonly number[], b: readonly number[]): boolean {
  return a.length === b.length && a.every((key, i) => key === b[i]);
}

function randomNonce(): bigint {
  const parts = new Uint32Array(2);
  crypto.getRandomValues(parts);

  return (BigInt(parts[0]) << 32n) ^ BigInt(parts[1]);
}
