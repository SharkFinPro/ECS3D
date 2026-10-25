# Replication

The root `AGENTS.md` summarizes this topic and links here. Read this file before changing the code it describes,
and update it in the same change when that behavior moves.

## Snapshots, deltas and edit commands

Two paths. Structural state (project/scene/assets) goes as a full **snapshot** — a
packed binary blob built by `ProjectPacker` (the wire counterpart of `ProjectSerializer`, which remains
the JSON path for file save/load) — sent on join and rebroadcast after any structural edit; every view
rebuilds from it, atomically (a malformed packet leaves the current project intact). Per-tick motion
goes as a compact binary **stateDelta** (uuid + local transform per object; `data/Replication.{h,cpp}`).
Edits flow the other way as typed commands (`editComponent`, `sceneEdit`, `sceneControl`, `loadProject`,
`addAsset`, `renameAsset`, `removeAsset`) that only a connection authorized as `Role::editor` on an
`--edit` server may send. A third direction exists for `serverLog` alone: server → editor connections
only, unicast via `NetServer::sendToEditors` rather than broadcast to every connection — see [logging.md](logging.md). An `editComponent` a replicated view could not apply is reported through
`replication::logMissedComponentEdit` — debug for an absent object/component (routine, since the server
rebroadcasts to views that may be a round trip behind), error for a malformed or partially applied
payload (a real divergence). Some components refuse a payload before assigning anything, as the convex
mesh collider does for an over-limit count, an invalid hull or a non-finite offset. That refusal throws a
`RefusedPayload` from `Component.h`. The edit then reports `malformedPayload` instead of `partiallyApplied`.
(`sceneEdit` carries the prefab-instantiation op too — see [prefabs.md](prefabs.md).)

## Structural sceneEdit ops

`sceneEdit` also carries two ops for undoing a deletion: `removeSubtree` deletes an object and its whole
subtree immediately, promoting nothing, unlike `removeObject` (which promotes the removed object's
children to its own parent in the same call - `applyStructuralEdit`'s `removeObject` branch calls
`ObjectManager::deleteObjectsMarkedForDeletion` itself rather than waiting for the next tick, the way a
script-driven destroy does; `ServerApp::handleSceneEdit` re-snapshots right after); `restoreObject` rebuilds a subtree from an inline serialized body under a
parent at a sibling index, and is the one structural op that **preserves the body's uuids** rather than
reassigning them, because the undo history (`data/edits/EditCommand.h`) already names the removed
subtree's objects by those uuids. `restoreObject` also takes an optional `"adopt"` list, each entry naming
a still-live uuid, the sibling index it should hold under the restored object, and its own recorded local
Transform blob — undo of a `removeObject`, which needs to reclaim the still-live children
`deleteObjectsMarkedForDeletion` promoted to the removed object's own parent back under it in the same
atomic op (the body alone cannot carry them back: their uuids are already live, which the body-walk
collision check would refuse). The op validates the adopt entries before it changes the scene: it resolves
each uuid to a live object and checks its current parent against `restoreObject`'s own target parent,
checks for a repeated uuid or one the body itself already names, and computes the combined depth (the
body's own height, or one more than the tallest adopted subtree when any are given) - a mismatch, a
duplicate, or a combined depth that would exceed `maxObjectDepth` is refused rather than applied. On apply,
each named child is detached from that parent's list before the restored object is inserted (so its own index reads
against the list with the adoptees already removed, exactly the pre-removal list), then reattached under
it at its recorded index with its recorded Transform reloaded; a `restoreSubtree` failure after that point
puts the detached children back where they were before returning `failed`. The
three `sceneEdit` ops that create an object (`addObject`, `duplicateObject`, `instantiatePrefab`) may carry
a client-chosen `"uuid"` for what they create; the server honors it and picks its own when the field is
absent. A uuid that does not parse is a `malformedEdit`; the nil uuid, or one already in use, is
`rejected` with the scene untouched. It exists so the sender knows which object its own edit produced -
the undo history records the reverse edit against that uuid.
A `duplicateObject` made while the scene runs seeds the copy's live values from the source's live values
(`ObjectManager.cpp`'s `copyLiveValues`, through each component's `pack`/`unpack`; scripts excluded; components with no live/authored split are simply copied) and leaves the
copy's authored values equal to the source's, since stop discards runtime objects and authored values are what saves.
`reparentObject` rewrites the moved object's local transform after reattaching so its world placement is
unchanged (inverting that composition: the world offset is taken into the parent's frame and divided by its scale), and `ObjectManager::deleteObjectsMarkedForDeletion` applies the same `objects/WorldPlacement.h`
helper to the children of a deleted object as they move up a level (and promotes them into the deleted
object's own slot, preserving their relative order, rather than appending them after whatever already
followed it there).
`reorderObject` is `reparentObject`'s drop-BETWEEN-siblings counterpart: it carries a sibling index
alongside the (optional) parent, so an object can land at a specific position rather than at the end of
the list, and covers both a same-parent reorder and a move-to-a-different-parent-at-an-index in the one op.
The index is read against the target list **after** the object is removed from wherever it sits now; an
index past that list's end is `rejected` rather than clamped, and so is one that would cycle or leave the
object exactly where it already was. `Object::addChild`/`ObjectManager::addObjectToRoot` each have an
index-taking overload (clamping to the list's size) that both `reorderObject` and `restoreObject` build on.
`sceneEdit` also carries a `batch` op: `{ "op": "batch", "ops": [...] }`, a list of other ops (a nested
`batch` is refused as malformed) applied atomically - `applySceneEdit` dry-runs every op in order against a scratch copy
of the scene (`ObjectManager::makeScratchCopy`, an independent copy sharing uuids and tree shape, wired to
no `SceneManager`/replication/scripts) and only applies them for real, in order, if every one of them
would have; one message, one `applySceneEdit` call, one re-snapshot. It is how the editor's multi-object
delete/duplicate ([undo-redo.md](undo-redo.md)) becomes one atomic edit instead of N separate round trips.
`sceneEdit` also carries four scene script ops (`addSceneScript`, `removeSceneScript`, `reorderSceneScript`,
`setSceneScriptFields`), keyed by class name. `ServerApp::handleSceneEdit` routes them to
`replication::applySceneScriptEdit` (`data/scenes/SceneScriptEdits`), which changes the current scene's script
list. `applySceneEdit` refuses them, so a batch holding one is malformed.

## Asset mutations

The asset-mutation trio (`addAsset`/`renameAsset`/`removeAsset`, built/packed in `data/Replication.{h,cpp}`,
applied by `AssetRegistry`) all follow the **local-apply-then-send** shape: the editor mutates its own
registry for instant feedback, then sends the op and the server re-snapshots. **Rename is display-only for
the file assets and prefabs** — a `renameAsset` sets an optional `AssetRecord::displayName` override (threaded
through `serialize`/`loadFromJSON`/`pack`/`unpack` like every other field); the file on disk and `path` (the
registry key, and the name-key for prefabs/scenes) are untouched by it. **A scene rename changes the scene's
real name** — the op's `displayName` is the new name, `applyRenameAsset` sets it on the `SceneAsset` and re-keys
the scene's registry record, and the next snapshot replicates it. It is refused (the function returns false,
nothing changes, the server logs a warning and still re-snapshots) for an unknown uuid, a scene record with no
`SceneAsset`, or a name `replication::isSceneNameAvailable` rejects: blank, or the registry path key of any other
record (another scene, a prefab, a model or texture path) or another scene's name. The editor's name field asks
that same function, so the UI and the server agree. Renaming to the current name is a successful no-op. **Delete succeeds and references
dangle** — `removeAsset` drops the record; `GpuAssetCache`/`AssetRegistry` lookups already null-tolerate a
missing uuid so referencing slots just show "None" in the inspector and the viewport draws the missing-asset
placeholder (a magenta and black checker; see `GpuAssetCache::getRenderObject`). The editor warns before deleting by scanning its
replicated scenes + prefab bodies for the uuid ("referenced by N objects", `editor/AssetReferences.{h,cpp}`, headless); no server-side refusal or
cascade exists (a known gap, not yet scheduled). A Scene record is regenerated from the `SceneManager` on every
snapshot, so a display override on it would not survive; that is why a scene rename changes the name itself and a
scene delete removes the scene from the `SceneManager`. Undoing a delete re-adds the whole
record, rename override included — the `addAsset` payload carries an optional `displayName`.
**Deleting a scene** removes it from the `SceneManager` first (`SceneManager::removeScene`) and drops the
registry record only if that succeeded, because the record would otherwise come back with the next
snapshot. `removeScene` refuses (false, nothing changed) the loaded scene, the only scene of the project,
and any scene while the run status is not stopped, because Stop discards the play-session history that
holds the undo. The server logs a refused removal; the editor checks its own status and active scene first
and sends nothing. The objects and scene scripts of the scene go with it. Undo re-adds it: the recorded
`removeAsset` carries `SceneAsset::serialize()` as its body, and the undo `addAsset` hands that body back
so `applyAddAsset` rebuilds the scene under its own uuid with its objects (uuids preserved) and scripts. A
body that does not parse or load adds nothing, and so does a scene add for a uuid the `SceneManager`
already holds. The history is cleared when the active scene changes, and a new scene becomes active, so
the `targetActive` refusal on undoing a scene add is a defensive guard, not a common path.
The Assets panel also creates scenes, scripts and empty prefabs.
It offers them from a "+" button and an empty-space context menu, not only the menu bar.
Each prefills a unique default name (`AssetNaming.h`).
The panel refuses a taken script name, and a taken scene or prefab name.
Scenes and prefabs share one name space, because the registry keys both by path.

## Project identity

A snapshot ends with a project load id: the server mints one at startup and takes the editor's on each
`loadProject`. An editor that sees another editor's load (`ProjectLoadTracker.h`) drops its save file and
project folder, so the next Save asks for one. The tracker resets on every connect.
The snapshot then carries a project fingerprint: a 64-bit hash of the project file the server loaded from
disk (`ProjectFingerprint.h`), zero for the built-in project or one an editor sent.
On the first snapshot of a connection, `ProjectIdentityCheck.h` compares it with the fingerprint of the
editor's own `--project` file and drops the save target on a mismatch, with a warning.
This catches `--host` against a server started on another project.
After any editor's `loadProject` the server reports zero, and line endings that differ across machines read as a
mismatch; both only drop the save target.
A server the editor spawned is trusted, and an editor with no `--project` has nothing to drop.

## Script-driven structural changes

Runtime structural changes from a *script* (spawn/destroy) take a third path: lightweight
`objectSpawned` (one packed `Object`) / `objectDestroyed` (a uuid) messages the client splices into/out of
its scene incrementally — kept off the full-snapshot path so frequent spawning stays cheap. Build/apply
live in `data/Replication.{h,cpp}`; a late joiner still gets the objects via the normal join snapshot.
The join, playerSlot, sceneStatus and editStatus payloads are built and parsed there too
(`replication::buildJoin`/`parseJoinNonce`, `buildPlayerSlot`/`parsePlayerSlot`,
`buildSceneStatus`/`parseSceneStatus`, `buildEditStatus`/`parseEditStatus`); each parser returns nullopt on a
truncated payload, and `parseSceneStatus` also on a status byte that is not a `SceneStatus`.
