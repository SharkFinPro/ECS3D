# Replication

The root `AGENTS.md` summarizes this topic and links here. Read this file before changing the code it describes,
and update it in the same change when that behavior moves.

## Snapshots, deltas and edit commands

Two paths. Structural state (project/scene/assets) goes as a full **snapshot** — a
packed binary blob built by `ProjectPacker` (the wire counterpart of `ProjectSerializer`, which remains
the JSON path for file save/load) — sent to the joining connection alone on join and rebroadcast to
everyone after any structural edit; every view rebuilds from it, atomically (a malformed packet leaves
the current project intact). Per-tick motion goes as a compact binary **stateDelta**
(uuid + local transform per object; `data/Replication.{h,cpp}`).
A snapshot ends with the terrain descriptions (uuid, voxel size, chunk count, palette) of every terrain in the
sender-side `TerrainStore`; chunk data never rides it. The server reconciles its store with its asset registry before
every snapshot it sends. Unpacking reconciles the view's store with the new registry and applies each description in
place, so the store survives snapshot rebuilds and a terrain keeps its volume unless its voxel size changed. A
description naming a uuid with no Terrain record is ignored, and a repeated uuid resolves to the last description.
Chunk data travels in its own `terrainChunks` message (`data/terrain/TerrainReplication.{h,cpp}`): the terrain uuid,
then per chunk its coordinate, a revision, the chunk's `hash()` and the `encodeChunk` bytes. An entry with no bytes
is hash-only: it asserts that the receiver already holds the chunk with that hash. `unpackTerrainChunks` refuses a
payload that is truncated, repeats a coordinate, carries revision 0 or a hash that differs from the decoded chunk, or
has trailing bytes. The server owns the revision counter (`TerrainStore::nextRevision`, `stampAllChunks`), and a view
records the numbers it is sent (`TerrainStore::setChunk`, `revision`). `applyTerrainChunks` applies an entry only when
its revision is higher than the one the store holds for that coordinate, so a late or duplicate copy is stale. A
hash-only entry is accepted only when the held chunk (the air chunk, for an absent coordinate) has that hash and is
otherwise reported as missing. A message carries at most `maxTerrainChunksEntries` entries, so a sender splits larger
sets and a receiver's decoded memory stays bounded. Revisions describe the volume, so replacing the volume clears
them while the counter keeps rising; removing a terrain, or reconcile dropping it, forgets both. Every server-side
change to a terrain volume must go through `TerrainStore::setChunk` (or be followed by `stampAllChunks`) so its
revisions describe it.
The server streams every terrain's chunks to each joining connection after its join snapshot (`TerrainStreamer`,
`data/terrain/`). The stream starts nearest the joiner's player (its `PlayerController` slot's position, brought into
the terrain owner's local frame; the world origin without one) and works outward. `ServerApp` pumps it at most once per
fixed timestep of wall time (20 ms), even while the scene is stopped or paused: at most 256 KiB per connection per
pump, about 12.5 MiB/s, in messages of at most `maxTerrainChunksEntries` entries. A peer that reads slower than
that eventually reaches the transport's 64 MiB send-queue cap and is dropped; bulk terrain is meant to move onto its
own paced connection later. The server stamps every loaded chunk with a revision after loading terrain files, so
a joiner applies them. A project load empties the volumes and restarts every stream, which then ends at once; the
server logs "Streamed N terrain chunk(s) to connection C" when a connection's stream runs out. A client or editor
passes each `terrainChunks` message to `TerrainViewSync::apply`: a malformed one is refused with a warning and the
store is untouched, an applied one invalidates the bounds of the Terrain components bound to that terrain, and the
view logs `Terrain <uuid> complete: N chunk(s)` when its held count reaches the snapshot's chunk count. After each
snapshot unpack the view calls `observeSnapshot`, which resets every volume (and so the revisions) when the project
load id differs from the previous snapshot's; the first snapshot is not a change.
A hash-only entry that does not match what a view holds makes `TerrainViewSync::apply` return `outcome.request`: a
`terrainChunks` message of hash-only entries (`makeChunkRequest`, recognized by `isChunkRequest`) carrying the revision
and hash the view was told. `TerrainEditEcho` is the server side of terrain edit echo. It records the chunks an edit
changed (`recordEdit`) and requests (`requestFull`), and `flush`, at most every `terrainEchoInterval`, sends each
recorded chunk once with its latest state: hash-only to a connection that was the only sender of it since the last
flush (its prediction already holds the content), full to every other, and a requested chunk in full to its requester
only.
On Start of a stopped scene the server keeps each terrain's chunk map (`TerrainPlaySnapshot`; the capture copies the map of
chunk pointers, not chunk data). Stop restores it, and so does a scene switch away from a running or paused scene:
each coordinate whose chunk pointer differs (changed, added or erased) is set back through `TerrainStore::setChunk`
with a fresh revision and requeued on every connection's stream, and the server logs "Restored N terrain chunk(s)
after Stop". A coordinate the exhausted revision counter refuses is logged as not restored. A terrain created since
Start is left alone, as is one whose voxel size changed; a project load drops the snapshot.
An editor's File > Open (not an editor started with `--project`, whose server reads the files itself) carries the
project's terrain by upload. `EditorApp::onLoadProject` loads the terrain files into its own store, packs
`loadProject` with their real chunk counts, then sends the chunks as `terrainChunks` messages (`TerrainUpload`, paced
like a stream, a mutation message that only the authorized editor may send). The server's `handleLoadProject` resets
its volumes and records each description's chunk count as an expectation (`TerrainStore::expectChunks`); the snapshot
packs the expected count for a terrain still awaiting its upload, so views know their target. `applyTerrainUpload`
sets each uploaded chunk with the server's own revision (the entry's is ignored), `requeue` sends the chunks on to
every connection, including the uploader, and the expectation clears when the terrain holds the expected count
(`Terrain <uuid> uploaded: N chunk(s)`). Play (`SceneControlOp::start`) is refused while any expectation is pending
(`pendingUploadReason`), and the editor refuses Play itself while a terrain holds fewer chunks than its description
says (`incompleteTerrainReason`) or while the snapshot of its own project load has not come back
(`ProjectLoadTracker::awaitingOwnLoad`; until then its store still holds the files it read), so a refused Play never
queues a history swap. A load the server refuses never returns that snapshot, so Play waits until the project is
opened again. While an upload is pending the server does not log stream finishes, since each uploaded batch revives
the streams. The editor pumps the upload at most once per 20 ms, the server's fixed step, and a dropped connection
stops it: the project must be opened again. On the server side the uploader's disconnect drops the expectations it
set, so Play is not held for every editor; the terrain stays incomplete until a project is opened again. The upload
uses the editor's own connection even for a server it spawned on the same machine; it also serves a remote `--host`
editor, so it stays where telling a spawned server the project root would not.
A terrain created while a project is open arrives as an `addAsset` with `assetType` `"terrain"`, the canonical record
path (`terrainAssetPath`) and a `body` holding the description as JSON (`terrain::addTerrainAssetJson`,
`data/terrain/TerrainAddition.{h,cpp}`): `voxelSize`, `chunkCount` and a `palette` of `name`, `albedoTexture` and
`specularTexture`, the field names of the terrain index file. The wire has no terrain field, so the existing `body`
string carries it; the registry record never holds a body. `applyAddAssetWithTerrain` (`Replication.h`), which the
server and the creating editor both call, checks the description first (`canApplyAddedTerrain`: it parses,
`isValidDescription` holds, the chunk count is at most `maxAddedTerrainChunks`, and the store holds no terrain under
that uuid), so a refusal changes neither registry nor store. Then it registers the record and `applyAddedTerrain`
creates the empty volume and, for a positive chunk count, records the upload expectation. A terrain `addAsset` with no
body only registers the record, as before, and the snapshot reconcile gives it a default empty terrain. On the server,
`ServerApp::onTerrainCreated` restarts the terrain's join streams (the store already holds the terrain when the
snapshot reconcile runs, so `replacedVolumes` would not see it) and makes the sender the uploader when no one is, so
its disconnect drops the expectation. The editor then uploads the chunks as for a project load; the snapshot that
follows the add reports the description, and Play is refused until the upload completes.
Edits flow the other way as typed commands (`editComponent`, `sceneEdit`, `sceneControl`, `loadProject`, `addAsset`,
`renameAsset`, `removeAsset`, `terrainEdit`, and the editor's `terrainChunks` upload) that only a connection
authorized as `Role::editor` on an `--edit` server may send. A `terrainEdit` message
(`data/terrain/TerrainEdit.{h,cpp}`) carries the terrain uuid, a stroke id and one operation: a brush, a redistance
box or a set of whole chunks. `unpackTerrainEdit` bounds only what the wire can say (a valid brush, a non-empty box,
at most `maxTerrainChunksEntries` distinct valid coordinates whose chunks decode to the stated hash); limits that
depend on the volume belong to the apply step. `applyTerrainEdit` (`data/terrain/TerrainEditApply`) bounds the
operation against the volume, applies it through `TerrainStore::setChunk` with one fresh revision for every changed
chunk, consumes no revision for an edit that changes nothing, and changes nothing when it refuses (unknown terrain,
invalid operation, too large, revisions exhausted).
`ServerApp::handleTerrainEdit` runs it between ticks, as the message arrives. A malformed edit logs
`Refused a malformed terrain edit (N bytes) from connection C` and a refused one logs
`Refused a terrain edit for terrain <uuid> from connection C: <reason>`; neither changes the store, and a good edit
logs nothing, since a stroke sends many. The changed chunks go to `TerrainEditEcho::recordEdit` with the sender's
connection id. An edit sends no snapshot and requeues nothing on the join streams: the echo reaches every joined
connection, and a chunk a stream has not sent yet is read from the live volume, and a duplicate of an
edited chunk that reaches a view twice is refused as stale. The run loop flushes the echo once `terrainEchoInterval`
(50 ms) has passed and something is recorded (`terrainEchoDue` in `ServerPolicy.h`), to the joined connections in
connection-id order: hash-only to the one connection that was the only sender of a chunk since the last flush, full to
the rest. A view whose hash-only entry does not match what it holds answers with a request; the editor sends the
`outcome.request` of `TerrainViewSync::apply` (a player never receives hash-only entries, so the client sends none),
and the server routes a `terrainChunks` message whose entries are all hash-only (`isChunkRequest`) to
`TerrainEditEcho::requestFull`, which sends those chunks in full to the requester alone at the next flush. A
disconnect forgets the connection's requests and its standing as a sender (`TerrainEditEcho::forget`), and a project
load drops every recorded edit and request (`TerrainEditEcho::clear`), since they describe volumes that were replaced;
a terrain the snapshot reconcile adds, drops or replaces likewise drops its own pending records
(`TerrainEditEcho::forgetTerrain`), so a terrain re-added under one uuid never flushes the old edits' air.
The snapshot build reconciles the store with the asset registry before packing, which can add a terrain, drop one or
replace a volume (a Terrain asset removed and added again under one uuid); `captureVolumes` and `replacedVolumes`
find those terrains and the server restarts their join streams, from the terrain's origin since no one connection
owns the restart, so a connection streams a terrain added after it joined and does not keep a stale plan for a
replaced one. A third direction exists for `serverLog` alone: server → editor
connections only, unicast via `NetServer::sendToEditors` rather than broadcast to every connection — see
[logging.md](logging.md).

Every snapshot is sent for a `net::SnapshotReason` that `broadcastSnapshot` requires of its caller, and `ServerApp`
records its payload size (without the type byte the traffic counters add), build time and recipients in a 32-entry
`net::SnapshotLog`; the stats line reports the largest since the previous line.

The join reply (edit status, snapshot, scene status) and the `playerSlot` answer are likewise unicast to
the joiner via `NetServer::sendTo`. A connection is answered on its first join only; a repeat join is ignored
and logged once per connection (`planJoin` in `ServerPolicy.h`). An `editComponent` a replicated view could
not apply is reported through `replication::logMissedComponentEdit` — debug for an absent object/component
(routine, since the server rebroadcasts to views that may be a round trip behind), error for a malformed or
partially applied payload (a real divergence). Some components refuse a payload before assigning anything, as the convex
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
collision check would refuse; the same check refuses a nil uuid anywhere in the body). The op validates the adopt entries before it changes the scene: it resolves
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
`rejected` with the scene untouched (a `restoreObject` body is held to the same rule for every uuid it
carries, root or descendant). It exists so the sender knows which object its own edit produced -
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
placeholder (a magenta and black checker; see `GpuAssetCache::getRenderObject`). The editor warns before
deleting by scanning its replicated scenes + prefab bodies for the uuid ("referenced by N objects", `editor/AssetReferences.{h,cpp}`, headless); no server-side refusal or
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
already holds. A scene add whose name another scene or any registry path key (a prefab, say) already holds is
renamed with a " (N)" suffix, and so is a scene loaded from a snapshot or file. Any other add (prefab, model,
texture, script) whose key belongs to a record of a different type is refused instead, with a warning
(`applyAddAsset` returns false and the server logs the refusal); a same-type prefab updates its body.
The history is cleared when the
active scene changes, and a new scene becomes active, so the `targetActive` refusal on undoing a scene add is a
defensive guard, not a common path.
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
disk and the index of each terrain under its root (`fingerprintProject` in `ProjectFingerprint.h`, so different terrain
is a different project), zero for the built-in project or one an editor sent.
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
