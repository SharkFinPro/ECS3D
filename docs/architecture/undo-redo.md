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
skipped rather than refused. The `replaceAsset` kind exists for that recording: an `addAsset` over a uuid - or a
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
