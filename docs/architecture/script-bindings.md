# Script binding coverage

The root `AGENTS.md` summarizes this topic and links here. Read this file before changing the code it describes,
and update it in the same change when that behavior moves.

Each *Bindings provider exposes its component's full public surface except
for internal bookkeeping, listed here so a future gap is a deliberate decision, not an oversight. Every
component's own `serialize`/`loadFromJSON`/`pack`/`unpack` stays unexposed the same way across all five
(project/wire plumbing, not gameplay state), not just Transform's. `TransformBindings` also leaves out
`getWorldUpdateID`/`markReparented` (cache-invalidation bookkeeping for colliders, not scene data). `RigidBodyBindings` leaves
out `getPendingForces`/`clearPendingForces` (the queue `applyForce` already writes to; PhysicsSystem
drains it). It also leaves out `setFalling`/`getNextFalling`/`setNextFalling` (PhysicsSystem's own double-buffered
falling state; `isFalling` is the read-only query scripts get). The same goes for `getStackedLoad`/`setStackedLoad`/`getHeldImpulse`/`setHeldImpulse`
(PhysicsSystem's per-tick record of what rests on the body and what its support has held).
It leaves out `getDriveStep`/`setDriveStep` too: the velocity gravity and queued forces gave it this tick.
It also leaves out `getSupportNormal`/`setSupportNormal`/`getNextSupportNormal`/`setNextSupportNormal`,
the contact normal its support pressed on it with.
`ColliderBindings` covers the shared trigger, layer and mask. It also covers each shape's offset and size:
box size, sphere radius, capsule radius and height, and the convex mesh's offset and read-only vertex count.
A shape's accessors fail safely on a collider of another shape. `CameraBindings` covers every field on
`Camera`; its setters clamp fov/near/far to the same valid range as the editor UI (`Camera::minFovDegrees`/
`maxFovDegrees`/`minNearPlane`/`minFarPlaneFor`), so a script cannot produce a degenerate projection.
`InputUtilsBindings` already covers everything `InputState` queries;
`setKeysPressed`/`setFocused`/`setMouse`/`setView`/`clearMouseDeltas`/`commitInputEdges`/`removeSlot` stay
unexposed because they are the write side ServerApp feeds from the network and the per-tick bookkeeping
that resets it - in this design scripts only consume input, so that side belongs to ServerApp - and
there is no player-agnostic aggregate for mouse position/delta/scroll/buttons because, unlike a key or
focus, a position has no sensible "any player" combination; only the per-object reads make sense there.
`cursorRayForObject` turns the slot's view into a world ray from the camera object it names, and reports none
when any link of that chain is missing.
`PlayerControllerBindings` covers the component's only field, `playerSlot` - `getPlayerSlot`/`setPlayerSlot`
plus `has` - reached via `World.tryGetPlayerController` (including a script's own object, by passing
`ScriptBase.EntityId`). `PlayerController::setPlayerSlot` does not constrain the value it is given, so
neither does the setter: a negative or unassigned slot matches no connected player, so the object reads
no input. Script components are reachable managed-side only (instances live in `Bridge._instances`, so no
native binding exists): typed through `World.tryGetScript<T>`/`ScriptBase.getScript<T>`, or by class name
through `World.tryGetScript(uuid, className, out ScriptHandle)`, whose `tryGetField` reads only
`[ExposeToEditor]` fields and whose `tryInvoke` runs under the target's fault gate and refuses `ScriptBase`'s
lifecycle methods.
The current scene's scene scripts are reached through `World.tryGetSceneScript<T>` or, from any script,
`getSceneScript<T>` and `requireSceneScript<T>`, which logs one error per caller instance and type for each stretch when
none is attached. A faulted scene script is skipped, as an object script is.
