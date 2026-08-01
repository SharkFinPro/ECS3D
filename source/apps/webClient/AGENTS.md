# AGENTS.md — ECS3D Web Client

> **Living document.** Keep this current. When architecture, conventions, workflows, dependencies, or
> structure change, update this file in the same change. Keep it concise. Read this before making changes.
>
> **This is a standalone project.** Like `apps/launcher`, it shares no build step or runtime with the C++
> engine — no CMake target, no CLR, no Vulkan. What it *does* share is the **wire protocol**, and that
> coupling is the whole point of the app. See "The contract with C++" below; the root `AGENTS.md` is the
> reference for everything on the other side of it.

## Project Overview

- **ECS3D Web Client** is a browser port of **`ECS3DClient`** (`source/apps/client`): the lightweight
  runtime view. It connects to an authoritative `ECS3DServer` over WebSocket, renders the replicated
  scene, and sends input. It is **not** an editor and **not** a server.
- **It simulates nothing.** No physics, no scripting, no prediction, no reconciliation, no interpolation
  — matching the C++ client exactly (`ROADMAP.md` E2/E3 park the last two). Every visible position comes
  from the server's per-tick `stateDelta`.
- Built with **Next.js 15 / React 19 / TypeScript 5.8**, rendering through a vendored copy of the
  **WebGPU port of VulkanRenderer** (see Vendored renderer).
- **WebGPU-only, no fallback.** The C++ client hard-requires Vulkan and has no degradation path; this
  mirrors that with an explicit error page rather than a second renderer.

## Build & Run

`npm install`, then `npm run dev` → http://localhost:3100. Also `npm run build`, `npm run lint`.

**The dev server is on 3100, not Next's default 3000, deliberately:** 3000 is `net::defaultPort`, the
port `ECS3DServer` listens on, and the common case is running both on one machine. Don't "fix" it back.
A `.claude/launch.json` config named `ecs3d-web-client` starts it.

**The server must be running its WebSocket backend.** `Transport.cs`'s `Protocol` field selects the wire
transport for the whole process, and a browser cannot open the raw TCP socket the other backend uses.
Both backends are wire-compatible, so this is a one-line change on the C++ side.

Server address mirrors `ECS3DClient`'s `--host`/`--port`, as env instead of argv — copy `.env.example`
to `.env.local`:

| Variable | Default | C++ counterpart |
|---|---|---|
| `NEXT_PUBLIC_ECS3D_HOST` | `127.0.0.1` | `--host` / `ConnectOptions::host` |
| `NEXT_PUBLIC_ECS3D_PORT` | `3000` | `--port` / `net::defaultPort` |

**There is no singleplayer.** `ClientApp::connectToServer` spawns a child `ECS3DServer` for the default
`--launchLocalServer` case; a web page cannot start a process, so a reachable host is always required.

## Layout

| Path | Mirrors | Responsibility |
|---|---|---|
| `src/ClientApp.ts` | `apps/client/ClientApp.{h,cpp}` | Connect, join, dispatch messages, send input, drive a frame. |
| `src/app/` | — | Next shell. `page.tsx` loads `ClientView` via `dynamic(..., { ssr: false })` (**required**, see Gotchas); `ClientView.tsx` owns the canvas, the rAF loop and the status readout. |
| `src/net/Protocol.ts` | `libs/protocol/Protocol.h` | `MessageType`, `Role`, `Message` writer, `MessageReader`. |
| `src/net/NetClient.ts` | `libs/net/NetClient.{h,cpp}` + `Transport/WebSocketBackend.cs` | The two C++ halves collapse into one class — the browser owns the socket natively. |
| `src/data/` | `libs/data/` | The read-only ECS: components, `GameObject`, `ObjectManager`, scenes, `AssetRegistry`, `ComponentRegistry`, `ProjectPacker`, `Replication`. |
| `src/render/` | `libs/render/` | `RenderSystem`, `GpuAssetCache`, `InputCapture`. |
| `src/renderer/` | — | **Vendored.** The WebGPU engine; treat as a dependency. |
| `public/assets/` | `apps/server/defaultAssets/` | Models + textures, plus the font and cubemap the engine loads eagerly. |
| `public/shaders/` | — | Vendored WGSL. |

## The contract with C++

**The wire format is the entire coupling, and it is unforgiving.** `Message` is an append-only byte
vector fed by `std::bit_cast`, so the wire is tightly packed little-endian with **no alignment padding**.
Every size below is load-bearing; one wrong offset desynchronizes the rest of the message:

| C++ type | Bytes | Note |
|---|---|---|
| `MessageType`, `Role` | 1 | explicitly `: uint8_t` |
| `ComponentType`, `AssetType`, `SceneStatus` | **4** | unsized `enum class` → `int` |
| `bool` | 1 | |
| `float`, `int32_t`, `uint32_t` | 4 | |
| `std::size_t` | **8** | only in `inputState`'s key count |
| `glm::vec3` | 12 | no padding |
| uuid, string form | 4 + 36 | length-prefixed; objects, scenes, asset records |
| uuid, raw form | **16** | `ModelRenderer`'s model/texture/specular **only** |

Framing: **no length prefix and no header.** The WebSocket transport delimits messages; byte 0 is the
`MessageType`, the rest is payload. Before any protocol message, the client sends one handshake frame,
`[0xFF][role][token UTF-8]`, which the C# transport consumes and never delivers upward.

**Two ordering traps** that look like typos and are not:
- `Transform::pack` writes **position, scale, rotation**; the state delta writes **position, rotation,
  scale**. They differ.
- `Script::pack` writes className *then* fields, but `Script::unpack` reads **only** fields — the class
  name is consumed by `Object::unpack`, which needs it to find the right script first.

**When a component gains a field, it must be threaded through `pack`/`unpack` on both sides.** That is
the same rule the root `AGENTS.md` states for `serialize`/`loadFromJSON`; here it spans two languages,
and nothing will catch a mismatch at compile time.

## Porting Notes (match upstream, including its quirks)

- **Transform hierarchy is not TRS.** `Transform::getPosition/getRotation/getScale` compose with the
  parent by **adding** position and rotation and **multiplying** scale — a rotated parent does not orbit
  its children. The server composes identically, so `Transform.ts` must too, or every parented object
  lands somewhere the server didn't put it. `ROADMAP.md` D5 tracks fixing it upstream; change both together.
- **`Camera`'s fov / nearPlane / farPlane do nothing**, exactly as upstream — the projection is hardcoded
  45° / 0.1 / 1000 in the renderer. They are still replicated. `ROADMAP.md` A2.
- **A deleted asset dangles by design.** `AssetRegistry` lookups null-tolerate a missing uuid and callers
  skip; there is no server-side refusal or cascade (`ROADMAP.md` B1).
- **Input is raw GLFW key codes** — the server's `InputState` is keyed by them, so `InputCapture`
  translates `KeyboardEvent.code` rather than inventing an encoding (`ROADMAP.md` F1 would remove them).
  Mind the button order: `MouseEvent.button` is left=0/**middle=1**/**right=2**, GLFW is
  left=0/**right=1**/**middle=2**. They swap on the way across.
- **Input is de-duplicated.** Only resend when discrete state changes, or on any frame scroll is non-zero
  (scroll has no resting position). A client that spams unchanged input clobbers the shared server-side
  `InputState` every frame.

## Player slots — connect exactly once

`ServerApp::assignPlayerSlot` gives each connection the **lowest free slot**, freed on disconnect. So
**every extra connection, however short-lived, shifts this client up a slot** — and a slot no
`PlayerController` carries breaks two things at once, in ways that look unrelated:

- `resolvePlayerCamera()` returns null, so the view silently falls back to *another player's* camera.
- `inputState` lands in `InputState[slot]`, which no script reads, so the player does not respond to
  input at all (right-drag mouse-look appears broken while the bytes on the wire are perfectly correct).

That is why **`ClientApp.create()` does not connect** — `ClientView` calls `connect()` only after the
StrictMode `cancelled` check, so the discarded first mount never opens a socket. Do not move the
connect back into construction, and do not add a reconnect that runs before the old socket is closed.

The status bar shows **`slot N (no player object)`** in red when this happens, since it otherwise reads
as a rendering or input bug. It is usually benign — a scene with no player objects, or more clients
than the scene defines players for.

## Freecam (this client only)

`ClientApp.setFreecam()` detaches the view from the player object and hands it to the engine's built-in
free-fly camera. **`ECS3DClient` has no such mode** — it always renders through its own player camera.
The nearest precedent is the editor's **View** combo, which picks between free-fly and a scene camera.

Two behaviours are worth knowing before changing it:

- **The mouse is not forwarded while in freecam.** Right-drag and the wheel belong to the local fly
  camera there, so `sendInput` zeroes the mouse block; otherwise looking around would turn your player
  at the same time. This is exactly `EditorApp::sendInput`'s rule ("forward the mouse only while the
  viewport looks through a scene camera"). **The keyboard still forwards from either view**, also
  matching the editor — so WASD flies the camera *and* drives your player simultaneously.
- **The fly camera keeps its own pose across toggles**, so leaving and re-entering freecam returns you
  to where you left it rather than to the player. That falls out of `RenderSystem.useFreeFlyCamera`,
  which pushes the camera's stored pose on the handover; it is not seeded from the player's view.

Bound to **`** (Backquote) plus a status-bar button. Deliberately not a letter: A-Z, Space and the
arrows are all forwarded to the server as GLFW key codes, so a letter would also mean something in game.

## Deviations from the C++ client

Each is forced by the browser, and marked `DEVIATION:` at the top of the file it affects.

- **Async construction.** `WebGPUEngine.create` and the WebSocket connect are both async, so the
  constructor splits into `ClientApp.create()`.
- **The loop inverts.** `while (isActive()) { ... }` would hang a tab; `frame()` is driven by
  `requestAnimationFrame` from `ClientView`.
- **Asset loading is async**, so `GpuAssetCache.getRenderObject` returns `null` until the mesh and both
  textures resolve — objects pop in a frame or two after the snapshot instead of stalling it. A failed
  load is cached as a permanent miss so a 404 isn't retried every frame.
- **No singleplayer** (no child process) and **no `setReflectivity`** (the vendored `RenderObject` drops
  it; upstream it only fed the ray tracer).
- **The inbox is capped** (`NetClient.maxInboxMessages`). A hidden tab stops `requestAnimationFrame`,
  so the frame loop — and with it the inbox drain — stops while the socket keeps delivering ~50
  `stateDelta`s a second, about 40KB/s of unbounded growth. The C++ client cannot hit this; its loop
  always runs. Trimming drops only the oldest **`stateDelta`**, which is lossless: `packStateDelta`
  restates *every* object's transform each tick, so applying just the newest lands on the same state.
  Snapshots, component edits, spawns and destroys are stateful and are never dropped.
- **`GameObject`, not `Object`** — the upstream class name shadows the JavaScript global inside its own
  module. The path still mirrors upstream.

## Performance invariants

Three optimisations carry the large-scene case. Each looks removable in isolation; measured on a
544-object scene at 800x600, they took CPU frame time from **17.0ms to 4.0ms**.

- **Superseded state deltas are dropped at drain** (`NetClient.drain`). A delta restates *every*
  object's transform, so only the newest matters. Without this a slow frame lets deltas pile up and
  draining all of them makes the next frame slower — a feedback loop that turns a dip into a spiral.
  Measured 9.27 deltas applied per frame before, 0.43 after; the `net` phase went 9.13ms -> 0.37ms.
  **Never restore a plain poll-until-empty loop.**
- **`getObjectByUUID` is a Map, not a scan** (`ObjectManager.byUUID`). `unpackStateDelta` looks up
  every entry of every delta; the linear scan upstream uses is fine for a 16-byte C++ uuid compare but
  cost ~300k 36-char string compares per delta here. 2.10ms -> 0.10ms per 544-lookup sweep. The index
  is filled by `registerUUID` from `unpack`/`loadFromJSON`, **not** `addObject` — an object's uuid is
  unknown when it is added — and pruned in `removeObject`.
- **`RenderObject` transforms upload once per frame, and only when changed** (`dirty` +
  `flushTransform`, called from `Renderer3D.renderObject`). The setters used to upload immediately, and
  `RenderSystem` sets position, scale and rotation every frame, so each object paid three matrix
  compose+invert+transpose passes and three `writeBuffer` calls. **1435 -> 23 buffer writes per frame**,
  because most objects in a large scene never move. `variableUpdate` went 4.57ms -> 0.69ms.

What is left is the renderer itself (~3.0ms CPU encode): **11,767 `drawIndexed` and 12,356
`setBindGroup` per frame, ~83% of it the shadow pass** — 3 lights x 6 cube faces x 541 objects, 18 of
the 20 render passes. That is the next thing worth attacking (cap shadow casters, cull per cube face,
or cache static geometry in the cube), along with frustum culling (`ROADMAP.md` A3) and dynamic-offset
uniforms to collapse the per-object bind groups. Server-side, `ROADMAP.md` E1/E4 would cut the delta
at the source.

## Verification

There is no test runner; verification is done in the browser against a **real `ECS3DServer`** — which is
the point, since the wire format is the coupling and nothing checks it at compile time.

`window.__ecs3dClient` is the live `ClientApp` (its private fields are reachable at runtime), mirroring
webGPUTest's `window.__wgeRenderer`. Two caveats carry over from that project: **rAF is paused while the
tab is hidden** (`document.hidden`), where the canvas also collapses because layout is 0x0, and
screenshots time out on a continuously-rendering app. So drive frames manually:

```js
const app = window.__ecs3dClient, engine = app.renderer;
const canvas = document.querySelector("canvas");
Object.defineProperty(canvas, "clientWidth",  { value: 640, configurable: true });
Object.defineProperty(canvas, "clientHeight", { value: 480, configurable: true });
canvas.width = 640; canvas.height = 480;
engine.getSwapChain().recreate(true);

engine.getLogicalDevice().getDevice().pushErrorScope("validation");
for (let i = 0; i < 90; i++) { app.frame(performance.now()); await new Promise(r => setTimeout(r, 16)); }
await engine.getLogicalDevice().getDevice().popErrorScope();   // null == the frame is clean
```

Then check `app.getStatus()` (expect a scene name, a non-zero object count and a player slot >= 0), and
`drawImage` the canvas into a 2D canvas to count non-black pixels. **Disable the grid first**
(`engine.getRenderingManager().getRenderer3D().disableGrid()`) — it covers a large share of the frame,
so a healthy count with it on proves very little. On the default project at 800x600, scene geometry
alone is ~60k non-black pixels across a few hundred distinct colours.

`app.assetCache.models` / `.textures` map a resolved path to the loaded object or `null` — a `null` is a
permanent miss (a 404 is cached so it isn't retried every frame), which is the fastest way to spot an
asset the browser is missing from `public/assets/`.

## Vendored renderer

`src/renderer/` and `public/shaders/` are copied from the **webGPUTest** project
(`C:\Users\Shark\Desktop\webGPUTest`), itself a port of `SharkFinPro/VulkanRenderer`. **Read that
project's `AGENTS.md` before touching anything under `src/renderer/`** — it documents the colour-space
rules, the light ABI duplicated across 8 WGSL files, MSAA sample-count requirements, and a list of
WebGPU gotchas that have each bitten it.

Only the **live** tree was vendored: `components/`, `utilities/`, `WebGPUEngine.ts`, `EngineConfig.ts`.
The pre-restructure tree it keeps as dead reference, and `src/tests/`, were deliberately left behind.

Three local changes, marked `ECS3D CHANGE:` rather than `DEVIATION:` because they diverge from webGPUTest
rather than from VulkanRenderer — in each case moving *back* toward what VulkanRenderer actually does:

- **`GlbLoader.ts` is new**, and `Model.load` dispatches on the extension. webGPUTest supports `.obj`
  only and substitutes procedural meshes for its two `.glb` test assets; **every ECS3D asset is `.glb`**
  and the `AssetRegistry` can name a new one at runtime, so a real loader is required. It covers the
  glTF 2.0 subset needed for one merged mesh — positions, normals, UVs, indices, node transforms.
  Materials, animation, skinning and sparse accessors are ignored (ECS3D binds its own textures per
  `ModelRenderer`, so a model contributes geometry only).
  **Note `.glb` UVs are NOT v-flipped** — glTF's origin is top-left like WebGPU's, unlike OBJ's — and
  a primitive whose node transform has a **negative determinant gets its winding reversed**, per spec.
  That last part is invisible without culling and load-bearing with it.
- **Back-face culling is restored to upstream's settings.** webGPUTest set `object`, `shadow`,
  `ellipticalDots`, `noisyEllipticalDots` and `cubeMap` to `rasterizationStateNoCull` because it
  substituted single-sided *procedural stand-in meshes* for the `.glb` assets it could not load. That
  reason disappeared with `GlbLoader`, so all five are back to `rasterizationStateCullBack`, matching
  `VulkanRenderer/source/components/pipelines/pipelineManager/PipelineConfigRenderObject.h` exactly
  (`texturedPlane`, `magnifyWhirlMosaic`, `curtain` and `bumpyCurtain` are `NoCull` upstream and stay
  that way). Without this the first-person view is *inside* the player's own mesh, and its back faces
  fill the entire frame. **If you vendor a newer renderer drop, re-check this table against upstream.**
- **`RenderingManager.doRendering` only pushes the free-fly pose while that camera is enabled**, and
  reads the frame's view back from `Renderer3D` instead of from the fly camera. webGPUTest pushed it
  unconditionally, which was fine there (every test drives the fly camera) but here **overwrote the
  scene `Camera` component's pose every frame** — and since `RenderSystem.updateCamera` *disables* the
  fly camera when a component camera takes over, a disabled camera never runs `processInput`, so the
  view froze at its configured start position. Upstream C++ has the same guard (`RenderingManager::render`
  pushes the free-fly pose only while the scene view is focused). Reading the view back from
  `Renderer3D` matters too: lighting, shadows and the projection must all use whichever camera is
  actually in charge, not the idle fly camera.

**Two assets are loaded eagerly by engine construction** even though this client draws neither:
`WebGPUEngine.create` awaits the Roboto font, and `PipelineManager.create` awaits `getCubeMapTexture()`.
Both live in `public/assets/`; deleting them breaks startup, not just the features that use them.

## Assets

The server replicates asset **paths**, never bytes — `GpuAssetCache` resolves `assets/models/x.glb`
against the client's own root, which here is `public/`. So **`public/assets/models` and
`public/assets/textures` must mirror `apps/server/defaultAssets/`**; a scene naming an asset the browser
doesn't have silently renders nothing. `ROADMAP.md` **B3** (asset delivery to remote clients) is what
would eventually remove the duplication.

## Development Principles

- **Mirror the C++ path and class name** for anything that has a counterpart, the way `src/renderer/`
  mirrors VulkanRenderer. Every ported file opens with a comment naming the file it ports.
- **Read-only means read-only.** No `pack()`, no `serialize()`, no edit commands. `editComponent`,
  `sceneEdit`, `addAsset` and friends are the *editor's* return path; this client only ever receives.
- Do not add client-side prediction, interpolation or local simulation without the C++ client gaining
  them first — divergence here is a desync, not a feature.
- `PascalCase` filenames matching the exported class; `camelCase` methods. Comments explain *why*, are
  ASCII-only, and never narrate history.
- Don't add dependencies. The app has three (`next`, `react`, `react-dom`) and needs no more.
