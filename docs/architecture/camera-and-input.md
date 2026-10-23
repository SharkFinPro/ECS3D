# Camera and player input

The root `AGENTS.md` summarizes this topic and links here. Read this file before changing the code it describes,
and update it in the same change when that behavior moves.

`Camera` is a plain-field data component (`direction`, `fov`, `nearPlane`, `farPlane`,
`active`) — position comes from the object's `Transform`, so only the *look* needs its own field.
`RenderSystem::updateCamera` finds the active `Camera` (optionally restricted to one object; that rule, the view
math and the View combo's labels live in the headless `data/objects/CameraSelection`), builds
`lookAt(pos, pos + q·direction, worldUp)` — `q` is the object's orientation, so the camera turns as the
object turns, but `worldUp` (not an orientation-derived up) keeps the horizon level — then disables
`vke::Camera`'s free-fly and calls `Renderer3D::setCameraParameters`; no active camera re-enables free-fly
(`RenderSystem::useFreeFlyCamera`, which also pushes the free-fly pose once on the handover — `render()`
only pushes it while the scene view is focused, so without that the component camera's last pose lingers).
The editor's **View** combo (Scene Status) picks what its viewport looks through: its own free-fly camera
(the default) or any object in the scene carrying a `Camera`, which is how you see a client's view — a
player camera is labelled with its `PlayerController` slot. A stale choice (object gone, `Camera` removed)
falls back to free-fly. **Field of view (FOV) and near/far drive the actual projection**:
`RenderSystem::updateCamera` also pushes the active `Camera`'s fov/near/far into `Renderer3D::setProjectionParameters` (only when they
differ from what was last applied), and the free-fly fallback (`RenderSystem::enableFreeFlyCamera`) resets
it to a named default (45/0.1/1000, `vke`'s own defaults) unless that default is already applied.
`setProjectionParameters` throws for a degenerate triple; `RenderSystem` catches that and logs once, though
the `Camera` setters already clamp to a valid range so it should not fire in practice.
One upstream caveat carries through: raster geometry is clipped at roughly twice the near plane,
since the projection maps depth to -1..1 while Vulkan clips at 0. A client picks its
*own* camera via the player↔object association (`PlayerController.playerSlot` + a `Camera` on the same
object) using a **nonce-over-broadcast** handshake: the client tags its `join` with a random nonce, the
server echoes `(nonce, slot)` back over the existing broadcast (`NetServer` has no targeted-send path), and
only the client whose nonce matches keeps it — chosen over adding a targeted-send ABI to the C# transport,
which would have meant touching both backends for one bit of routing. The handshake's build and parse live in
`data/Replication.{h,cpp}` (see [replication.md](replication.md)). `PlayerScript` mouse-look rotates the
object's `Transform` from `input.mouseDelta()` while right-click is held (matching the free-fly camera's own
gesture) and zeroes `RigidBody` angular velocity each tick so a collision-induced spin can't fight the look;
movement is relative to the `Camera.direction` (via `CameraBindings`, see [scripting.md](scripting.md)) rotated by that yaw, not a hardcoded
forward axis. **The editor must not gate forwarded mouse input on `io.WantCaptureMouse`** (unless its viewport stops being an ImGui window): its 3D viewport
*is* an ImGui window under the dockspace, so that flag is set whenever the cursor is over the scene, and
gating on it silently swallows the right-drag mouse-look. `EditorApp::captureGatedInput` (called from `sendInput`) instead forwards the mouse
only while the viewport looks through a scene camera (in free-fly the right-drag belongs to the editor's own
camera, so forwarding it too would turn the player at the same time) **and** `RenderingManager::isSceneFocused()`
is true — the same signal `vke` gates its free-fly camera on. The keyboard still gates on
`io.WantCaptureKeyboard`, which only trips for text input, so WASD (the movement keys) reaches the game from either view.
Connections are bound to slots by `PlayerSlotTable` (`data/PlayerSlots`).
The editor joins with a nonce too, so it learns its slot the same way a client does.
Its Scene Status "Player" combo (`EditorApp::displayPlayerSelector`) sends `possessSlot` to move its input to another slot.
That is not a mutation message, but the server honors it only on an edit-mode server, from the authorized editor connection.
The server refuses a slot another live connection holds.
`ServerApp::handlePossessSlot` answers every request, granted or refused, with a `playerSlot` for that connection's nonce.
The editor follows only that answer.
On a confirmed switch it resends its input for the new slot and points its View at that player's `Camera`.
`findPlayerCamera` finds that camera and is shared with `ClientApp`; a player with no `Camera` leaves the view alone.
The join reply just records the slot, and an unanswered request times out so the combo is not stuck disabled.
