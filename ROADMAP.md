# ECS3D Roadmap

> Companion to `AGENTS.md`. `AGENTS.md` describes the engine **as it is**; this file is a short list of
> what to build next. Items keep stable IDs (`D5`, `C1`, …) so issues and PRs can reference them.

This is deliberately small. The engine is a solo project built one PR at a time, so the plan is scoped to
match: a handful of concrete, mostly self-contained items to do **now** and **next**, and a flat
**later** list that parks the bigger ideas without pretending they're scheduled. Pull things up from
*Later* as they become the most valuable next step — don't try to burn down the whole list.

Layer discipline from `AGENTS.md` applies to everything here: fields in `data`, behavior in the owning
system, UI in `editor`. `data` never gains a Vulkan or ImGui include; the server never links
`render`/`editor`.

## Baseline — what exists today

- **Components:** Transform, ModelRenderer, RigidBody, Box/SphereCollider, LightRenderer, Script,
  PlayerController, Camera.
- **Rendering:** one draw per object per frame, no culling, no material asset, point + spot lights only.
- **Physics:** GJK/EPA narrow phase, sort-by-min-X broad phase, velocity-level forces, layers/masks,
  triggers, raycast/overlapSphere.
- **Networking:** TCP/WebSocket; full project snapshot on join and after every structural edit; an
  uncompressed 50 Hz per-tick transform stream for every object.
- **Editor:** hierarchy tree, inspector (objects + assets), asset browser, editable prefab bodies, scene
  play/pause/stop, GPU picking, single-slot selection.
- **Absent:** audio, animation, particles, runtime UI, materials, undo, automated tests.

---

## Now — correctness and a safety net

Do these first: they fix things that are wrong at the foundation, and they're mostly contained to one
library each.

### D5 — True TRS transform hierarchy · M

**Today.** `Transform::getPosition/getScale/getRotation`
([Transform.cpp:30](source/libs/data/objects/components/Transform.cpp:30)) combine with the parent by
*adding* position, *adding* euler rotation, *multiplying* scale. There's no matrix composition anywhere,
so rotating a parent spins its children in place instead of orbiting them — a turret, a wheel, a held
weapon all break.

**Do.** Compose `world = parentWorld · T · R · S` as matrices, cached per object with a dirty flag off
the existing `getUpdateID`. Store rotation as quaternions internally, keep euler degrees editor-facing.
Add world↔local helpers. Audit every consumer of the additive rule (`RenderSystem`,
`SceneQueries::boxWorldMatrix`, `BoxCollider::generateTransformedMesh`, collider gizmos). The wire format
doesn't change — `stateDelta` already sends local transforms and the receiver recomposes.

### D1 — Mass-correct collision response · M

**Today.** In [PhysicsSystem.cpp](source/libs/sim/PhysicsSystem.cpp): `applyForce` does `velocity += force`
(never divided by mass); `handleCollision` splits impulse evenly regardless of mass ratio; gravity is
`gravity * dt * 0.1f` and inertia carries another `* 0.1f`, so `mass` and `-9.81` don't mean anything
physical. A bowling ball and a ping-pong ball bounce identically.

**Do.** Separate force (`a = F/m`, integrated) from impulse (`Δv = J/m`); expose both to scripts. Proper
impulse resolution split by inverse mass, with a `restitution` field on `RigidBody`. An explicit
`isKinematic`/infinite-mass flag. Remove the `0.1f` fudge factors and make gravity real m/s². Inertia from
the actual collider shape. Positional correction with slop so resting stacks stop jittering. Expect to
retune `DefaultProject` and the example scripts in the same PR.

### H1 — Serialization round-trip tests · S–M

**Today.** CI runs `ctest` but zero tests are registered
([cmake-multi-platform.yml:108](.github/workflows/cmake-multi-platform.yml)), so it proves nothing. The
`serialize`/`loadFromJSON` and `pack`/`unpack` contracts — which replication, save/load, and the registry
all ride on — are verified only by someone noticing a value went missing.

**Do.** Wire up a framework (Catch2 or GoogleTest) via `FetchContent` + `enable_testing()`, then cover the
highest-value cases: per-component `serialize → loadFromJSON` and `pack → unpack` round-trips, a full
`ProjectPacker`/`ProjectSerializer` project round-trip, and `Message`/`MessageReader` underflow handling.
A "golden project" load→save→reload equality check catches a whole class of regressions in one test. Keep
it small; the point is a net under the correctness work above, not full coverage.

---

## Next — editor feel and cheaper motion

High-value once the foundation is solid. `C2` and `A2` share one upstream projection change; do `C1`
before `C2`.

### A2 — Camera projection control · S (+ M upstream)

**Today.** `Camera` exposes `fov`/`nearPlane`/`farPlane` in the inspector and **none of them do anything**
— the projection matrix is hardcoded 45° / 0.1 / 1000 in `RenderInfo::getProjectionMatrix()`. Flagged
deferred in `AGENTS.md`.

**Do.** Upstream (`VulkanRenderer`): centralize projection on `Renderer3D`, add a projection setter
alongside `setCameraParameters`, expose the matrix. Then push the component's fov/near/far from
`RenderSystem::updateCamera`, and add an orthographic mode while the wire format is open. The same upstream
change unblocks `C2` — land it once.

### C1 — Undo/redo · L

**Today.** Every edit is fire-and-forget to the authoritative server (`editComponent`, `sceneEdit`,
`addAsset`, …); nothing records the prior value. A mistyped scale or deleted object is permanent.

**Do.** An `EditorCommand` (`apply`/`revert`) abstraction; route every mutating UI path through one
`CommandHistory` instead of calling `m_netClient->send` directly. Snapshot the *before* value on
`IsItemActivated`, commit one entry on `IsItemDeactivatedAfterEdit` (a whole drag = one undo, matching the
prefab editor's coalescing). Structural commands serialize the removed subtree so redo rebuilds it with the
same uuids. Bounded history + Ctrl+Z/Ctrl+Shift+Z. Scope v1 to *this editor's own* commands; a remote edit
may make an entry a no-op — document it, don't pretend it's transactional.

### C2 — Viewport transform gizmos · M

**Today.** Positioning means typing numbers into the inspector. A full implementation plan already exists
in [docs/viewport-gizmos.md](docs/viewport-gizmos.md) — ImGuizmo, the exact upstream changes, hotkey
conflicts, the delta-clobbering guard. Not yet built.

**Do.** Follow that doc: expose view/projection/viewport-rect (the `A2` upstream change), build ImGuizmo
against the engine's ImGui target, add `libs/editor/TransformGizmo.{h,cpp}`, emit the same
`buildComponentEdit` the inspector already emits. Route the drag through `C1` so it's one undo entry.

### E1 — State-delta compression (cheap tier) · M

**Today.** `replication::packStateDelta` ([Replication.cpp](source/libs/data/Replication.cpp)) writes,
for **every** Transform object **every** tick whether it moved or not, the uuid as a 40-byte string plus
three full `vec3`s (~76 B/object/tick). At 50 Hz a 1,000-object scene is ~3.8 MB/s per client for a scene
where five things moved.

**Do.** Just the cheap, self-contained wins for now (they're most of the payoff): (1) assign each object a
`uint32` network id at spawn, send the uuid↔id map once in the snapshot — saves 36 of 40 bytes; (2) dirty
tracking off the existing `Transform::getUpdateID`, so only changed objects are sent; (3) a per-entry
field mask for which of pos/rot/scale is present. Quantization, baselines/acks, and interpolation are
`Later` (`E2`) — they need the prediction/interpolation work alongside them.

---

## Later — parked, not scheduled

Real ideas, kept as one-liners so they're not lost. Each keeps its original ID. Promote one into *Next*
when it's genuinely the most valuable thing to build — most of these are only worth it once a real project
built on the engine actually needs them.

**Rendering**
- `A1` Material assets — share a look across many objects; inline-body asset like prefabs.
- `A3` Frustum culling + render-object reuse — stop submitting/allocating per hidden object. Needs `A2`.
- `A4` Directional light + shadow controls — a sun; upstream `DirectionalLight` type.
- `A5` Skeletal animation — the single biggest thing separating this from "a game". Large, upstream-heavy.
- `A6` Particle / VFX component — expose the existing upstream GPU particle systems. Cosmetic, client-side.
- `A7` Skybox / environment — per-scene cubemap + fog; cheapest visual upgrade.
- `A8` Runtime 2D UI / HUD — health bars, menus; new `libs/ui` over the upstream `Renderer2D`.

**Assets & pipeline**
- `B1` Asset reference integrity — server-side delete policy + a reference index; today deletes dangle.
- `B2` Prefab instance linkage & overrides — editing a prefab doesn't touch placed instances. Do `C1` first.
- `B3` Asset delivery to remote clients — content-hash + chunked transfer so a remote client isn't blank.
- `B4` Import pipeline, thumbnails, hot reload — grid view, model thumbnails, texture/model watching.
- `B5` Standalone game export — produce a runnable build; needs `B1`'s dependency walk.

**Editor & tooling**
- `C3` Multi-select & multi-edit — widen `EditorSelection` to a set with a primary.
- `C4` Profiler & stats overlay — per-phase server timings + bandwidth; every perf item needs it.
- `C5` Editor UX pack — focus-on-selection, hierarchy search, copy/paste/duplicate, grid snapping, prefs.

**Physics**
- `D2` Capsule/convex/mesh colliders + character controller — a character is a capsule, a level is a mesh.
- `D3` Broad-phase rework, sleeping, single-pass narrow phase — replace the O(n²) sort-and-scan; halve GJK.
- `D4` Joints & constraints — doors, ragdolls, vehicles. Needs `D1` first.

**Networking**
- `E2` Client-side snapshot interpolation — remote objects stutter at 50 Hz; buffer + interpolate.
- `E3` Client-side prediction & reconciliation — kill local input lag. Most invasive item; needs `E1` acks.
- `E4` Interest management — stop sending every client every object; needs a targeted send on the transport.
- `E5` Incremental structural replication — stop re-snapshotting the whole project on every edit.
- `E6` Unreliable channel for motion — a UDP backend so one dropped packet doesn't stall everything.
- `E7` Session robustness — protocol versioning, reconnect/resume, heartbeat, input validation + rate limit.

**Scripting & gameplay**
- `F1` Input action mapping — named actions/bindings + gamepad instead of raw GLFW key codes.
- `F2` Component binding coverage — scripts can't touch ModelRenderer/LightRenderer/Collider/add-component.
- `F3` Object enable flag, tags, layers — no way to turn an object off; small and unblocks pooling.
- `F4` Timers, coroutines, script diagnostics — `invoke(delay)`, `Debug.log` to the editor console.
- `F5` Script→client gameplay events — the missing third path (events, not state) for VFX/audio/UI.

**Audio**
- `G1` Audio subsystem — the engine is silent; new client-side `libs/audio` (miniaudio), `AudioSource` data.

**Infrastructure**
- `H2` Performance & determinism harness — headless `--bench`, stress scenes, state-hash determinism check.
- `H3` Structured logging & diagnostics — levels/categories, file output, stream server logs to the editor.
