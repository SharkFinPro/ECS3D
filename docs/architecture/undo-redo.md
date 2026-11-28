# Editor undo and redo

The root `AGENTS.md` summarizes this topic and links here. Read this file before changing the code it describes,
and update it in the same change when that behavior moves.

## Model

`data/edits/EditCommand.h` and `EditHistory.h` hold the undo/redo stack. It is
headless by design (it links `ECS3DData` and nothing UI-side). The design decision that shapes it: **undo is a new edit, not a
local rewind** - undoing sends an ordinary reverse edit back through the normal replication path and waits
for the rebroadcast like any other change, so the server stays the single source of truth and every
connected view converges the same way. A command records its target uuid(s) and a before/after state in
replicated form (a component's `serialize()` blob, an object's name/parent, a whole removed subtree, a
whole asset record); the payload to send is built on demand from `Replication.h`'s own builders, rather than
stored as a built `net::Message` (a `Command` stays a plain, copyable, comparable value that way).
**Validation happens once, at the moment of undo/redo - not on every snapshot**, which would invalidate
the whole stack on every structural edit even in single-user editing. It compares the command's
after-state (undo) or before-state (redo) against the live scene/registry; a mismatch refuses the whole
edit and drops that entry and everything older still on the stack being popped (undo is sequential -
skipping a dropped entry to reach an older one would apply reverts out of order), naming which uuid
conflicted. Every kind is reversible today. `removeObject` undoes through `restoreObject`'s
`"adopt"` field (see [replication.md](replication.md)): `ObjectManager::deleteObjectsMarkedForDeletion`
promotes the removed object's children up to its own parent (preserving their world placement) rather than
deleting them, so undo has to both recreate the removed object and reclaim those already-live children
back under it, in one atomic `Command` payload - `EditCommand::buildUndoJSON`'s `removeObject` case builds
exactly that from the recorded subtree, and its `validateForUndo` checks the object is absent, the
recorded parent exists, and every recorded direct child is still living under that parent (where
`deleteObjectsMarkedForDeletion` left it) before undo is attempted. `isReversible()` stays a real check
(not a constant `true`) as the extension point for a future kind with no faithful reverse. Every structural
kind *is* reversible: `removeComponent` undoes through `addComponent`'s optional `"data"` field (the removed
component's own `serialize()` blob, loaded onto the freshly created component in the same op that creates
it, rather than the blank default a bare `addComponent` normally makes) and redoes through the ordinary
`removeComponent` op; `duplicateObject`/`instantiatePrefab` undo through `removeSubtree` (named by the
created root's uuid alone - it deletes the whole subtree immediately, unlike `removeObject`, so it needs
none of the descendants' uuids) and redo by re-sending the original creating op (`instantiatePrefab`'s
command also carries the prefab asset's body as it was at record time, since `AssetRegistry` lets a
prefab's body be replaced in place under the same uuid - a re-save between undo and redo - and
`validateForRedo` refuses rather than silently instantiating whatever the registry holds now). Redoing that way - rather
than replaying a captured subtree - means a duplicate's or prefab instance's descendants get fresh uuids
each time it is recreated, while the created root keeps its uuid: the three creating ops (`addObject`,
`duplicateObject`, `instantiatePrefab`) must name the uuid of what they create, `RecordEdits` records no command
for one that does not (absent, unparseable or nil), and each redo re-sends the op with that same uuid
(`EditCommand::buildRedoJSON`), so the entry left on the undo stack still targets a live object. The editor
waits for the server to confirm a structural edit and rebuild the snapshot rather than applying it to its own
view up front, so the descendants' uuids are not known when the command is derived. **Every
editor mutation is recorded**: each of `EditorApp`'s mutation callbacks (component edit, scene edit, add
asset, rename/remove asset) asks `edits/RecordEdits.h` for the command before it sends, deriving the
before state from the replicated view while that view still holds it, and records what comes back; an edit
nothing faithful can be derived for (a stale view, an op with no matching kind, a creating op with no
usable uuid) is logged at debug and
skipped rather than refused. Terrain asset removal is recorded when the caller passes a `TerrainStore` that holds the terrain completely and a terrain create is recorded by the create flow, see
[Terrain assets](#terrain-assets). The `replaceAsset` kind exists for the other asset recording: an `addAsset` over a uuid - or a
prefab name - the registry already holds replaces a record rather than adding one (a prefab body edit,
"Save as Prefab" over an existing name, which mints a fresh uuid the name-keyed registry then discards),
and `addAsset`'s reverse would delete the prefab instead of restoring its previous body. A `renameAsset` of a scene
records the scene's name (its record's `path`) as the before value, since a scene has no display override, and
its undo and redo validations compare against that name. Both stacks are cleared wherever the authored scene they refer to is replaced: load project
(New/Open), scene switch, and (re)connect (any stashed authored history goes too). A snapshot clears them too when its active scene
differs from the one the history was recorded against, as after another editor's load or scene switch.
`edits/HistoryScope.h` tracks that scene. Play does not clear them:
pressing Start stashes the authored history (`edits/PlaySessionHistory.h`) and runs the session on a fresh
one, keeping authored commands apart from play-time ones (only structural and name edits made while playing
yield usable entries, since `Transform::serialize` reads authored values, so a live component edit records
before == after and is dropped); pressing Stop discards the play-session history and restores the stash, which is valid because `SceneAsset::stop()` rebuilds the authored
tree with uuids preserved. The swap happens when the request is sent, not when the status returns, since the
server applies edits in the order sent and an edit sent just after Start belongs to the runtime scene; a
`sceneStatus` the server reports before it has seen the request is stale and ignored: outstanding requests are
matched to statuses in order, so a stale one cannot confirm a later request. The reported status is
followed only for a start/stop made elsewhere. A pause/resume leaves the scene, and so the history, as it is.

## Unsaved changes

The editor's dirty flag is the headless `editor/UnsavedChanges.h`: the project is dirty when
`EditHistory::position()` differs from the position recorded at the last save, load or New, or when an edit
the history does not hold happened since. So undoing back to the save point reads clean, and redoing away from it
reads dirty. A position is the id of the entry on top of the undo stack, or a token for the base state when it
is empty. Ids come from one process-wide counter, never repeat and survive undo/redo, so undo followed by a
different edit at the same depth is a different position. Trimming the oldest entry or dropping the undo stack
(a clear of a non-empty history, or an undo refused at validation) gives the base state a fresh token, so a save point that is lost
stays dirty until the next save instead of matching by accident; a save point still on the stack survives a
trim. `SaveUI` reads the position through a provider the editor sets to `PlaySessionHistory::authoredPosition()`,
at save time and at check time. Undo and redo move the position when the request is built, before the server
confirms it; `reportUndoRejected`/`reportRedoRejected` have no caller, so a server-side rejection is not
reflected.

`SaveUI::markEdited` is only for an edit with no history entry (an asset op or scene edit nothing could be
derived for, `forgetSaveTarget`, an edit made while a play session runs): it latches until the next save. A
component value edit sends every frame, so `beginUncommittedEdit` holds the project dirty until the gesture
ends (`endUncommittedEdit`, from the inspector's and the gizmo's gesture-ended callbacks, which fire even when
the gesture committed nothing). During play the project stays dirty for any edit, because the play-session
history is discarded on Stop and the authored position does not move; a recorded edit also latches while the
client is disconnected, since the send was dropped. Opening a project during play drops the stash, and the
stopped report that follows keeps the history that replaced it, so the new save point still matches. Load and New take their save point after the load callback has cleared
the old history.

## Scene script panel edits

The `SceneScriptsPanel` is what sends the scene-script ops. While a widget is held it edits a panel-owned
copy of a script's fields, then sends one `setSceneScriptFields`. It keeps the copy until the echo arrives,
so the replicated view still holds the "before" state that undo records. A pending gate ignores structural
clicks until the list changes, since a duplicate would record a duplicate undo entry.

## Grouped entries

A history entry can also group several commands (`EditHistory::recordBatch`), sent and undone/redone as
one unit - the editor's multi-object delete/duplicate (see [editor.md](editor.md)) records one of
these instead of one entry per object. The editor does not apply a structural edit to its own replicated
view before sending it (see above), so that view is still pre-batch when `undo()`/`redo()` reverse a
group; each simulates the reverse on a scratch copy of the live scene (`ObjectManager::makeScratchCopy`),
validating and building each command's undo/redo op in turn against what the ones before it in that same
group would have left behind, and sends the collected ops as one `replication::buildBatch` - one Ctrl+Z
reverses the whole group in one sceneEdit. A single-command entry is unchanged: no scratch copy, no batch
wrapping, the same payload as before grouping existed.

## Scene script undo kinds

The four scene script ops each have an undo kind (`addSceneScript`, `removeSceneScript`, `reorderSceneScript`,
`setSceneScriptFields`), recorded from the pre-edit `SceneAsset`. Undo and redo validate them against the
scene passed to `EditHistory::undo`/`redo`; with no scene they are refused, naming the scene's uuid. `recordBatch` refuses
them, since group undo simulates on a scratch `ObjectManager` that has no script list.

## Terrain strokes

A finished sculpt stroke is one `terrainStroke` command (`EditCommand::terrainStroke`): the terrain's uuid and, for each
chunk the stroke changed, the chunk from before and after it (chunks are immutable, so the pointers are the
recorded state; null is air). `commandForTerrainStroke` (`edits/RecordTerrainEdits.h`) builds it from
`TerrainStroke::Finished::before` and the volume as it stands after the stroke, drops a chunk whose hash did not
change, and returns nullopt when none did, so a stroke that changed nothing records nothing.

Undo and redo validate against the `TerrainStore` passed as the last argument of `EditHistory::undo`/`redo`: a null
store or an unknown terrain is `targetMissing`, and a live chunk whose hash is not the recorded after chunk (undo) or
before chunk (redo) is `targetChanged`, both naming the terrain. The payload form is `PayloadForm::terrainChunks`:
`HistoryOutcome::terrainPayload` holds a `TerrainSetChunks` carrying every chunk's before (undo) or after (redo).
The caller sends it with `packSetChunksEdits`, which splits it into `terrainEdit` setChunks messages of at most
`maxTerrainChunksEntries` entries and `maxSetChunksMessageBytes`, and applies it to its own volume with
`predictSetChunks`, as a brush is predicted; the caller applies that prediction before the next undo or redo, or
validation reads a stale volume and refuses. Null and air are the same chunk when comparing. The factory throws
`std::invalid_argument` for an empty chunk list, a repeated coordinate or one beyond `maxChunkCoord`, since the server
refuses a setChunks message with either as a whole; `commandForTerrainStroke` keeps the first entry of a repeated
coordinate. `recordBatch` refuses a `terrainStroke`.

A material remap records the same way: `edits::predictTerrainRemap` (`edits/RecordTerrainEdits.h`) remaps the
editor's volume, captures each changed chunk's pointer from before, and returns the `remapMaterials` message with a
`terrainStroke` command built by `commandForTerrainStroke` (nullopt when the editor's volume held nothing it changed;
the message is still worth sending). The palette change is its own `replaceAsset` (`commandForTerrainPalette`), so
undoing a removal or move is two history entries.

A stroke entry pins every chunk it touched, before and after, so the depth limit alone does not bound memory.
`EditCommand::pinnedBytes()` sums `byteSize()` over those chunks (a terrain asset command counts the chunk map it
holds; 0 for every other command, no deduplication), and `EditHistory` keeps the total over both stacks exact
(`pinnedBytes()`). When recording leaves the total over `maxPinnedBytes` (512 MiB; a constructor takes another
budget), the oldest undo entries are trimmed until it fits. The entry just recorded is never trimmed, so a single
stroke over the budget is still undoable. A trim treats the save point like a depth trim: it gives the base state a
fresh token, a save point still on the stack survives and one that was trimmed never matches again.

### Terrain strokes in the editor

`TerrainBrushSession::finish` adds every chunk the release redistance changed that no brush touched to
`Finished::before`, with its pointer from before that redistance piece's commit, and sets `Finished::predicted` when the
redistance was also committed to the editor's volume. `TerrainBrushSession::Advance::finished` carries the stroke only
then (not for a replaced volume, lost editing or a stroke that recorded nothing). `EditorApp::finishTerrainStroke`
turns it into `commandForTerrainStroke` and records it on `m_playHistory.current()`, so a stroke drawn while playing
lands on the play history that Stop discards; the terrain returns to its Start state through the chunks the server sends
on Stop. The dirty flag is held (`SaveUI::beginUncommittedEdit`) from the press until the stroke ends.

Every end of an active stroke goes through `finishTerrainStroke`: a release, leaving the mode, a replaced volume, lost
editing, a lost connection (`cancelTerrainStroke`) and a send that throws while the release is sent (the session clears
the stroke first, so the end is still reported and the exception then propagates). Each releases the dirty hold. A
stroke that recorded a step moves the history position like any other edit; one that sent brushes but recorded none
calls `SaveUI::markEdited`, since the server changed and the history cannot reverse it. A stroke that sent nothing does
neither.

`TerrainUndoState` (`editor/TerrainUndoState.h`, headless) holds the decisions. While a stroke is active every undo and
redo is refused with "finish the terrain stroke first". While the final state of a released stroke has not arrived
only a request whose top entry (`EditHistory::nextUndoKind`/`nextUndoPrimaryUUID`, or the redo pair) is a
`terrainStroke` of the brush session's terrain is refused, with "waiting for the server's final terrain"; every other
kind proceeds. `reportHistoryOutcome` packs `packSetChunksEdits` first (a failure clears the history and sends
nothing), applies `terrainPayload` to the editor's volume with `predictSetChunks`, invalidates the terrain's bounds and
sends the messages. A send that fails partway is logged and leaves the view ahead of the server until the echo's hash
mismatch makes the view request full chunks.

The terrain a request changed is staged while its payload is built and bound to the in-flight gate when
`beginUndoRedoPending` begins it, replacing whatever an earlier request bound, so a structural request waits on no
terrain. The server answers with a hash-only terrainChunks echo that matches the prediction, and that message about
the bound terrain releases the gate (`noteTerrainChunks`), rather than a snapshot. A gate that already timed out
releases nothing and drops the bound terrain, so a late echo cannot release an unrelated later request.

## Terrain assets

A terrain's volume lives outside the asset registry, so its `addAsset` and `removeAsset` commands hold a
`TerrainRestore` (`edits/TerrainRestore.h`) beside the record fields: the description, a `ChunkMap` snapshot (shared
immutable chunks, nothing copied) and the display name. They are the existing kinds with an optional terrain part, not new
kinds. `makeTerrainRestore` sets the description's chunk count to the chunks held, since the server waits for exactly that
many after the addAsset and a count left from before a sculpt would hold Play forever.

Recording reads the store at the moment the op is sent. `commandForRemoveAsset` takes a `const terrain::TerrainStore*`
(default null, which records nothing for a terrain) and needs it to hold the terrain's description and a volume with at
least `description.chunkCount` chunks. A shorter volume is a join stream or upload that has not landed, and an undo would
re-add a partial terrain and lose the rest, so that delete is not recorded (logged at info; `markEdited` still holds the
project dirty). `chunkCount` is the stream's expectation and a sculpt does not refresh it, so a terrain whose sculpts
erased chunks reads short as well and its delete goes unrecorded, the safe side; the same reading holds Play and saving.
`commandForAddAsset` never derives a terrain command. `EditorApp::onCreateTerrain` sends the addAsset without recording
(`addAsset(..., recordHistory = false)`) and records `commandForTerrainAdd` once the chunks are in the store.
`EditCommand::addAsset`/`removeAsset` throw `std::invalid_argument` when the `TerrainRestore` names another uuid. A create is
therefore two entries, the asset and then the placement object (a grouped sceneEdit): the first Ctrl+Z removes the object,
the next removes the terrain asset, and redo brings them back in the opposite order, the asset first. Grouping them is
impossible, since a group must be all sceneEdit kinds.

Undo of a delete and redo of a create re-add the terrain. `HistoryOutcome::terrainRestore` is set beside the addAsset
`messagePayload`, which carries only the description. `reportHistoryOutcome` then calls `restoreTerrainAndSend` (`edits/TerrainRestore.h`): it applies the restore to the
editor's own registry and store through `applyTerrainRestore` (the same `applyAddAssetWithTerrain` a create uses, so the
snapshot that follows keeps the description and chunks instead of creating an empty default terrain), sends the addAsset,
then queues the chunks on `m_terrainUpload`, so the server never expects chunks the editor has no queue for. The server
expects those chunks, so Play is held until the re-upload lands. Any throw rolls the registry, store and queue back and
logs an error. The editor then clears the edit history, as it does for a refused restore, and when the throw came after the
send (`TerrainRestoreResult::failedAfterSend`) it also sends the removeAsset. Undo
of a create and redo of a delete send the removeAsset as any asset kind does and set `HistoryOutcome::terrainRemoved`,
whose pending upload the editor drops.

Validation is the asset kinds' (record absent or present), plus the store: a command that adds the terrain back is
`targetChanged` when the store already holds the uuid and `targetMissing` for a null store. Undo of a create does not
compare the volume, because strokes recorded after it sit above it on the stack and are undone first.

A palette edit is a `replaceAsset` of the terrain record: `commandForTerrainPalette` (`edits/RecordTerrainEdits.h`)
holds the description body before and after (`addTerrainAssetJson`), and undo and redo send an `addAsset` carrying that
body, which the server applies as a palette replacement ([replication.md](replication.md)). The registry record does not
change on a palette edit, so validation compares the live palette in the `TerrainStore` passed to `undo`/`redo`: a null
store or an unknown terrain is `targetMissing`, and a palette other than the command's after (undo) or before (redo)
palette is `targetChanged`. The editor applies an undo or redo payload that is such a replacement of a terrain its store
holds (`TerrainPaletteUndo::appliesLocally`) to its own store before sending it, as it does for any asset edit, so the
next undo validates against the palette the server is about to hold and not the one from before this step.

## Editor wiring

`EditorApp::undo()`/`redo()` are what read the stacks back: each peeks the top of the relevant stack with
`EditHistory::nextUndoIsReversible()`/`nextRedoIsReversible()` before calling `undo()`/`redo()`, and sends
whatever payload comes back for any reversible kind - every kind today, but a future non-reversible kind on
top would be refused with a log message and left in place rather than handed
to `EditHistory::undo()`/`redo()`, which would otherwise treat "a kind with no reverse" the same as a validation conflict and drop it (and
everything older beneath it) even though nothing about it is actually wrong. `reportHistoryOutcome` sends
whichever of `jsonPayload`/`messagePayload` `HistoryOutcome` set, matching `payloadForm()`: a sceneEdit
json payload (`addObject`, `reparentObject`, `reorderObject`, `renameObject`, `addComponent`) is chunked
into a `sceneEdit` message the same way `EditorApp::onSceneEdit` does for a normal edit; a networkMessage
payload (`componentEdit` and every asset kind) is sent as-is. Ctrl+Z/Ctrl+Shift+Z and an Edit menu (between
File and Window) call `undo()`/`redo()` through `requestUndo()`/`requestRedo()` (`EditorAppUndoMenu.cpp`; holding the chord repeats the step, and the in-flight gate below still paces it);
the menu items read `EditHistory::nextUndoLabel()`/`nextRedoLabel()` (built from
`EditCommand::describeForMenu()`) to name the action they would perform ("Undo Rename Cube") instead of
showing a bare "Undo"/"Redo", and disable themselves with nothing to act on, a read-only server, or a
request already in flight - `requestUndo()`/`requestRedo()` ignore a further request once one has actually
popped a stack entry (the in-flight gate, its injectable clock and the `gainedOneEntry`/`canActOnHistoryItem` checks are the headless `editor/UndoRequestGate.h`) until the server's rebroadcast lands (`handleSnapshot`/`handleEditComponent`) or a
short timeout passes, since undo validates against the editor's replicated view, which only updates on that
rebroadcast, and a second press inside one round trip would otherwise validate against a still-stale value.
A refusal is logged the same way for a stale target (`targetMissing`/`targetChanged`, naming the conflicting
uuid) or an empty stack. `targetActive` is a defensive guard for a scene asset op the server would reject
because that scene is the loaded one: undoing a scene's `addAsset` (New Scene) or redoing its `removeAsset`.
The history is cleared when the active scene changes, so it rarely fires. The editor reports "load another
scene first". Undo of a scene `removeAsset` re-adds the scene whole, since the command
records `SceneAsset::serialize()` as its body (`commandForRemoveAsset` reads the `SceneManager`); its redo
validation compares only type, path and display name for a scene, as the registry record has no body. `EditHistory::
reportUndoRejected()`/`reportRedoRejected()` exist for a server rejection of the edit undo()/redo() just
sent, but nothing calls them yet: today's `editComponent`/`sceneEdit` handling has no wire-level
acknowledgement back to the sender for a rejected edit to hook into (a failed edit is only logged
server-side, and sometimes answered with a resync snapshot) - the handlers are `ServerApp::handleSceneEdit`/
`handleEditComponent`.
