# Scripting runtime

The root `AGENTS.md` summarizes this topic and links here. Read this file before changing the code it describes,
and update it in the same change when that behavior moves.

## Runtime model, input and fields

`ScriptSystem` drives `ScriptBridge` (C# gameplay scripts) through the `ScriptRuntime` seam.
`ScriptEngine` implements it over `ManagedHost`. `ScriptSystem` is handed a factory for the runtime and does
not include the CLR host, so the suite compiles it and tests it against a fake. Native
`bindings/` expose Transform/RigidBody/InputUtils/World/Collider to C# via fn-ptr structs; each fn-ptr struct is
mirrored by a C# `[StructLayout(Sequential)]` struct and registered through `Bridge` (add new fields at
the **end** of both to keep the layout matched). `BindingContext` is the bridge from the static, C-ABI (application binary interface)
bindings back to the server's live scene: `ScriptSystem` points it at the current `ObjectManager` each
tick. The headless server has no GLFW window, so input is networked: clients send `inputState` (built by
`replication::buildInputState`, parsed by `replication::parseInputState` in `data/Replication.{h,cpp}`,
like every other wire payload), the server writes it into `InputState`, and `InputUtilsBindings` reads it
back for scripts. Input is
**per-player**: the transport tags each inbound message with a stable connection id (`NetServer::poll`'s
`senderId`, from a monotonic id the C# backends assign); `ServerApp` binds each connection to a **player
slot** on join (freed on disconnect via a transport disconnect callback + `NetServer::takeDisconnected`),
and `InputState` is keyed by that slot. A script reads *its own* player through `ScriptBase.input`
(`PlayerInput`), which resolves `object → PlayerController.playerSlot → InputState[slot]`; the global
`InputUtils` stays a player-agnostic aggregate. `PlayerController` is an ordinary replicated data
component (slot is editable + serialized), so possession is visible to the editor and to clients — the
hook the camera uses to pick "whose view" (see [camera-and-input.md](camera-and-input.md)). Mouse delta/scroll and key edges
(`wasPressedThisTick`) accumulate between fixed ticks and are reset per tick by
`InputState::clearMouseDeltas()`/`commitInputEdges()`. Forces
requested from a script are buffered on the `RigidBody` data (pending-force queue) and drained by
`PhysicsSystem`, keeping `scripting` independent of `sim`. Each carries a `ForceMode` (`RigidBody.h`,
mirrored by ScriptBridge's `ForceMode`) that says its units: a force or an impulse is divided by the mass,
a force or an acceleration acts for the tick's `dt` the way gravity does, and a velocity change is added
as it is. `PlayerScript` steers with velocity changes, so its movement does not depend on its mass. Five conventions worth inheriting: (1) reaching
another object's component is a **`tryGet`** (`World.tryGetTransform(uuid, out t)` → false when
absent/destroyed; it does not throw in the tick loop) — future component wrappers follow this; (2) a binding
that mutates scene structure can't touch the net layer, so it **buffers the change on `BindingContext`**
and the app drains + replicates it after the tick (see the spawn/destroy path in [replication.md](replication.md)).
Spawning is also deferred a level lower, in `ObjectManager` itself: `ScriptSystem`'s update, start,
stop and attach loops range over `getAllObjects()` and run script code inside the loop, so `addObject` cannot
append straight to that live vector without risking a reallocation mid-iteration. `ScriptSystem` holds an
`ObjectManager::ScriptPassGuard` around each of those loops; while one is alive, `addObject` queues the new
object instead (still immediately parented/started/`getObjectByUUID`-able) and `flushPendingAdditions`
splices it into `m_allObjects`/`m_objects` once the pass ends - called from `ServerApp::
broadcastStructuralChanges` at the same point `deleteObjectsMarkedForDeletion` drains a removal, so a
script-spawned object joins the scene the same way a script-destroyed one leaves it: after the pass, not
mid-iteration. The spawn bindings refuse while the `ObjectManager` is not started (a stopped scene; a paused
one stays started), so a constructor or `stop()` on a stopped scene cannot add to the authored tree;
(3) **sim→script events cross at the app, as plain data.** `CollisionSystem` records each tick's colliding
pairs and diffs them into enter/stay/exit uuid-pair lists; `ServerApp` hands those to
`ScriptSystem::dispatchCollisionEvent` (→ `onCollisionEnter/Stay/Exit` script virtuals) after the collision
pass — the same "buffer plain data, let the app carry it" shape as pending forces, so `sim` does not link
`scripting`. Dispatch returns before any object lookup for an object with no attached script, so contacts
between unscripted bodies cost nothing; (4) **sim→script *queries* cross by function-pointer injection.** Query behavior stays in a
`sim` system (`SceneQueries`: raycast/overlapSphere; the ignored object's same-body colliders are skipped too, but a
descendant with a rigid body of its own is not), and `ServerApp` injects its statics into
`BindingContext` (`setRaycast`/`setOverlapSphere`) at startup; the `World` bindings call through them. The
injected signatures use only shared types (`ObjectManager`/`glm`/`uuid`), so no library learns the other —
the pattern to reuse for any future sim query exposed to scripts; (5) **a read-only script binding for
data the object doesn't own outright** follows the same `Transform`/`RigidBody` shape even when nothing
mutates it: `CameraBindings` (`getDirection`/`has`) lets `PlayerScript` read its own `Camera` component so
movement can be relative to wherever the camera actually faces, degrading to the forward default
`(0,0,-1)` when the object has none — the same "safe missing-component" convention as `tryGet`, just
without the `tryGet` ceremony since `ScriptBase` constructs one for the script's own object (like
`transform`/`rigidBody`/`input`). `bindings/BindingCoverage.h` holds a table of every `ComponentType`
against bound/notYetBound/nativeOnly; a new enumerator with no row fails the build, so adding a component
without deciding its scripting story can't go unnoticed. A Script field entry may carry a `displayName`, the
`[ExposeToEditor]` label the Inspector shows; a bare attribute shows the field name.
It may also carry `"asset"` set to prefab, model or texture, from `[PrefabReference]`, `[ModelReference]` or
`[TextureReference]` (subclasses of `AssetReferenceAttribute`) on a string field. The Inspector and the Scene
Scripts panel draw that as that type's `AssetSlot`, and the value stays the uuid string. Script field edits are
validated (`ScriptFieldEdit.h`) against the instance's exposed fields before any setter runs; a mismatched field is
refused with a warning and does not fault the script. Exposed fields may be float, int, bool, string or
vector3: each type has a `getField*`/`setField*` export, and a string crosses as 8-bit Unicode text that the
native side frees with `freeString`. `TryConvertFieldValue` takes a string only for a string field and refuses one for
every other type, and the Inspector draws a string field as a text box.

## Script asset records

The server derives Script asset records for compiled classes, and `serialize` skips them. Before each snapshot,
`ServerApp::broadcastSnapshot` syncs the records with `ScriptRuntime::scriptClassNames` (`syncScriptClassAssets`).
Every record of a compiled class takes its `scriptKind`, "scene" or "object", so each picker skips the other's
classes; a class with no record gets a pathless one, with a uuid derived from the name. Once a compile has
succeeded, a pathless record of a class that stopped compiling is removed. A record whose class compiles cannot be
deleted in the Assets inspector, since the next sync would restore it. A later Script record naming the same class,
such as the new-script action's, merges into the existing one and fills in the path.

## Compilation and hot reload

A hot reload compiles before swapping (`reloadScripts` reports whether it did), so a compile error leaves the previous scripts running and `ScriptSystem` keeps its attachments; a missing or empty script directory still unloads them.
A project's own scripts, `<project root>/scripts/**/*.cs`, compile together with the built-in
`scripts/UserScripts` into one script assembly, at startup and on every hot reload.
`scripting::scriptDirectories` (`ScriptDirectories.h`) builds the list, built-in first, and `ServerApp` creates
its `ScriptSystem` after locating the project so it can pass the root.
The bridge's `init` takes the list as one string joined with newlines, and `ScriptSystem` polls every listed
directory for changes. A project without a `scripts` folder is normal and silent.
Files under `bin`, `obj` and dot-folders are skipped, so an IDE project there cannot inject generated sources.
A class declared in both a project script and a built-in script is a compile error naming both files.
The root is fixed at startup, so loading another project at runtime keeps the first project's scripts.
`ScriptSystem::pollScriptChanges(scene, dt)` makes the same throttled check for a scene that is not ticking:
`ServerApp` calls it every `run()` iteration on an edit-mode server whose scene is stopped (a paused scene is
left alone). After a successful reload it attaches the scene's scripts without starting them and reads their
fields back, then the server re-snapshots, so a new or edited script's exposed fields reach the Inspector before Play.
Two script classes that share a simple name fail the compile, with a message naming their files. The runtime finds
scripts by that name, so this holds across namespaces too; the parts of one `partial` class count as one class.

## Instance lifetime

A Script whose class cannot be instantiated (missing, failed compile, throwing constructor) is warned about
once and left unattached until the next hot reload or until its Script component is replaced (a stop
rebuild or scene switch), which retries it and warns again if it still fails. Field read-back skips a
missing or faulted instance, and discards a read that faulted it, so the saved values are kept in place
of defaults. `LogBindings` gives scripts `Log.trace/debug/
info/warn/error` through `ECS3DLog` under `LogCategory::script`, registered first in `registerBindings` so
everything the bridge itself logs afterwards - init, hot-reload, compilation - already has a binding to
write through. Scripts get `after`/`every` timers on `ScriptBase` (`Scheduler.cs`), ticked by the bridge's fixed update
inside the script's fault gate, so a throwing callback faults the script and time advances only on fixed
ticks (a paused or stopped scene freezes them). Timers belong to the script instance, so a detach or hot
reload drops them, and `every` skips missed intervals instead of catching up.
Instance lifetime tracks the live component set in both directions: a Script added to a
running scene is attached and started by the next tick, and the mirror image holds too - a script removed
mid-run (or whose object is destroyed) is stopped and detached by an orphan sweep that runs on every tick
and on every scene edit's snapshot. The sweep matches by component identity, not by (uuid, class) key, so
a re-added script of the same class gets a fresh instance rather than inheriting the stale one.

## Scene scripts

A scene can carry scripts of its own (`SceneAsset::getScripts`), written as a `SceneScript`: a managed sibling of
`ScriptBase` under a shared `ScriptInstance` base that holds the lifecycle virtuals, the `after`/`every` timers and
the `subscribe`/`publish` events. It has no object, so no per-object components and no collision callbacks.
Its managed instance is keyed by the scene's uuid and class name and created through `attachSceneScript`, which
instantiates only a `SceneScript` type, the mirror of `attachScript`, which instantiates only a `ScriptBase`.
Object lookups return object scripts only. The `SceneAsset` overloads of `ScriptSystem` drive both kinds in a fixed order: scene scripts start before
object scripts, run `variableUpdate` and `fixedUpdate` before them each tick in list order, and stop after them.
Scene scripts keep their own attached, started and unresolved bookkeeping and their own orphan sweep, matched by
component identity. The object sweep leaves scene scripts alone, and a rebuilt entry from `SceneAsset::stop()` gets a
fresh instance. `ServerApp` calls only the scene overloads. A scene script that cannot be instantiated, including a
class that is not a `SceneScript`, is warned about once and keeps its saved field values.
A scene script gets its managed `stop()` only if it was started, unlike an object script, which is stopped
whenever it is attached.
A `SceneScript` also gets scene-wide virtuals (`onSceneLoaded`, `onSceneUnloading`, `onObjectSpawned`,
`onObjectDestroyed`, `onPlayerJoined`, `onPlayerLeft`) through `ScriptSystem::dispatchSceneEvent`, which reaches
only started scene scripts. `start(scene)` delivers loaded, and `stop(scene)` delivers unloading before any
`stop()`. `ServerApp` adds one `onPlayerJoined` per bound slot after loaded. While the scene is not stopped,
`ServerApp` reports spawns and destroys from the script buffers and from a before/after uuid diff
(`SceneObjectDiff.h`) around each editor `sceneEdit`. It reports player joins, leaves and possession switches as
they happen. A scene script started mid-run misses `onSceneLoaded` and the start-time joins. A failed project load
that restarts the scene delivers unloading then loaded again. Handler spawns stay pending until `ServerApp` flushes
them right after each out-of-tick dispatch, and a stop discards what unloading handlers buffered. A
`setSceneScriptFields` edit is pushed to the live instance before the snapshot, since the snapshot reads instance
values back.

## Structural operations from scripts

`World.setObjectName(uuid, name)` renames a live object at once, refusing an unknown or marked-for-deletion object
or an empty name. It replicates through the same deduped `objectComponentsChanged` resync as the component ops
below, since `Object::pack`/`unpack` carry the name.
`ComponentOpsBindings` (`World.hasComponent`/`addComponent`/`removeComponent`/`getComponentTypes`) gives
scripts generic access to an object's component set, keyed by the same `ComponentRegistry` type-name
strings unpack/loadFromJSON use ("RigidBody", "Box", "Sphere", ...) rather than `ComponentType`'s packed
enum value: that value is the wire discriminator, assigned by enumerator order, so a name the managed side
looked up independently could drift from what the native side actually packs. Transform is refused by
add/remove (structural - every other system assumes an object has exactly one) but still queryable; Script
is excluded everywhere (adding one needs a class name this API does not take). An object already marked
for deletion this tick (`ObjectManager::isMarkedForDeletion`) refuses both too, since it is due to leave the
scene in the same tick's `deleteObjectsMarkedForDeletion` pass, after the structural broadcast that would
otherwise carry the change - a client would only see the deletion, not the change made just ahead of it.
add/remove apply to the object immediately - unlike `World.spawnObject`,
`Object::addComponent`/`removeComponent` for a non-Script type only ever touch `m_components`, a map
`ScriptSystem`'s `fixedUpdate`/`variableUpdate` loops (which range over the `ObjectManager`'s object list
and each object's `m_scripts` vector) do not read, so mutating one object's components mid-pass leaves both
loops' iterators alone, even for the running script's own object - so `hasComponent` called later in the
same tick already reflects it. Replication is still deferred and batched: the changed object's uuid is
recorded on `BindingContext` (deduped, so several add/remove calls against the same object in a tick still
cost one resync), and `ServerApp::broadcastStructuralChanges` sends each changed object's current packed
subtree (`Object::pack` recurses through children) as `objectComponentsChanged` after the tick - a
client/editor finds the root by uuid and unpacks it in place, which reconciles the component set and the
children at every level against what was packed. Still far narrower than the whole-project snapshot the
editor's own `sceneEdit` `addComponent`/`removeComponent` ops need for the same kind of change: a resync
only touches the changed object's own subtree, the same granularity `objectSpawned`/`objectDestroyed`
already use for other script-driven structural changes. Scripts can `publish`/`subscribe` named events through a
bridge-owned channel (`EventChannel`): delivery is synchronous, each handler runs under its own script's fault
gate, publishes nested deeper than 16 are refused, and only a live instance can subscribe (from `start()` on),
its subscriptions ending with it.

## Starter scripts

Example gameplay scripts live in `UserScripts/Starters/`, one public class per file.
`StarterUtils` holds their shared helpers (throttled lookup by name, `findPlayerBody`, `parseKey`, the Euler
rotation mirror of `RotationConvention.h`) and `StarterEvents` the event names and payload classes. Objects are
referenced by name, assets and prefabs by uuid string. `PlayerScript` exposes its keys as string fields
(W, A, S and D to move and the space bar to jump by default) that `parseKey` reads, re-parsing when a
value changes; the managed `Key` enum gained a space bar value for that.
