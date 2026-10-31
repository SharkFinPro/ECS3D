# Editor library

The root `AGENTS.md` summarizes this topic and links here. Read this file before changing the code it describes,
and update it in the same change when that behavior moves.

`ECS3DEditorLib` — ImGui editing UI: `ComponentEditor` (per-type handlers, sharing `AssetSlot` for asset
reference slots, which show `assetDisplay::name`, so a rename reads through, and only a record of the slot's own
type (`assetDragDrop::slotRecord`), else None), `ObjectGUIManager` (object tree - Delete and Ctrl+D act on the
whole current selection when it has 2+ objects, not just the row clicked: one atomic `batch` `sceneEdit`, one
undo/redo entry - see [replication.md](replication.md) and [undo-redo.md](undo-redo.md)), `InspectorPanel` (the
"Inspector" window — per-selection-kind dispatch) delegating the object kind to `ObjectInspector` (which, beside
the per-frame value send, coalesces a whole continuous edit - a slider drag mutates the component in place every
frame - into one before/after pair reported once no widget is active; it sends an object rename once, when the
Name field is left or the object stops being shown, never per keystroke - the headless `ObjectNameField.h`
decides when, and keeps the field in step with undo and outside renames; a multi-object selection
(`ObjectInspector::displayMulti`) shows the components shared across the selected objects - matched by
`componentFieldDelta::componentSignature`, a collider's subtype or a script's class name counting as part of the
"kind" - renders each shared component once against the first object, and propagates the top-level `serialize()`
key(s) that changed to the other objects' own components via `ComponentFieldDelta.h`'s field-delta helpers, so
an edit lands on the field that actually moved while each object's other fields keep whatever values they
already had; the disagreeing keys are threaded into `ComponentEditor::GuiHandler` as a `MixedFields` set (empty
on the single-selection path) so each differing field's own widget shows a distinct mixed state - a dash -
instead of silently displaying the primary object's value, via ImGui's `ImGuiItemFlags_MixedValue` for the stock
Drag/Checkbox/ColorEdit3 widgets this wraps (a Script field's bool still resolves a mixed click to true itself,
since the flag only changes the rendering), and hand-drawn for `accentCheckbox`/`accentSlider`, which are not
built on a stock widget; a residual "Mixed values" line still names the few fields with no per-widget indicator
(an asset reference slot, the collider mask popup, a non-numeric script field), and each object's edit still
goes out and is recorded through the normal single-object path, so undo records one entry per object; Add/Remove
Component and the name field are left out of this view for now, since wiring them up would mean deciding which
object they act on) and the asset kind to `AssetInspector` (per-`AssetType` views — read-only detail plus a
display-name rename field (a scene's name field commits once, when editing ends) and a delete button with a
reference-count warning for the flat file assets (it counts objects whose components or script fields mention the
uuid, in scenes and prefab bodies); the
**Prefab body is editable** — deserialized into a detached `TransientObject` and edited by a reused
`ObjectInspector`, see [prefabs.md](prefabs.md)), `EditorSelection` (shared kind-tagged selection slot holding
an ordered set of uuids rather than a single one, `Selection.h` - the back of the list is the primary, what the
Inspector/gizmo show; in the tree, a plain click replaces it, Ctrl-click toggles membership, and Shift-click
selects the contiguous range between an anchor uuid and the clicked row in the tree's current on-screen order,
which `ObjectGUIManager` tracks itself each frame since collapsed nodes and the sort mode both affect what "on
screen order" means; the range, row-action targets and sort rules live in the headless `ObjectTreeOrder.h`; the
viewport has its own rule, described under Viewport gizmos below), `SettingsPanel` (the "Settings" window — a
section nav beside the selected section's content, opening centered at a font-relative default size on first use
(a saved `imgui.ini` layout wins; `SettingsPanelLayout.h` holds that math) with the Keybinds columns sized from
their content, reading and writing `ECS3DSettings` directly since
preferences are local, not replicated; Appearance edits the `EditorTheme.h` palette tokens live; Viewport drags
the free-fly camera speed (`EditorCameraSettings`, applied to `vke::Camera::setSpeed` immediately) and the
viewport gizmo's Snap toggle + translate/rotate/scale step sizes (`EditorGizmoSettings`); Keybinds lists every
`EditorAction`, its current chord, and Rebind/Unbind/Reset controls, driving `KeybindDispatcher`'s capture mode
and showing a conflict modal - naming the holding action, no reassign option - when a rebind targets an
already-held chord), `KeybindDispatcher` (the one `vke::KeyCallbackEvent` listener that resolves a press against
a `KeybindTable` and calls the registered handler, suppressed while ImGui wants the keyboard; also drives the
Settings panel's rebind capture; actions registered with `KeyHold::repeatWhileHeld` (undo and redo) fire again
while their chord stays held, after `keyHoldInitialDelay` and then every `keyHoldRepeatInterval`, driven by the
headless `KeyHoldRepeat.h` and polled by `KeybindDispatcher::update()` once per frame; a handler with nothing
left to do calls `stopRepeat()`), `SceneScriptsPanel` (the "Scene Scripts" window: add, remove, reorder and edit
the current scene's scripts, docked on the Inspector's side; its pure decisions are the headless
`SceneScriptsPanelLogic.h`, and the field widgets are `ScriptEditor.h`'s `drawScriptFieldWidgets`, shared with
the object Script handler), `ConsolePanel` (the "Console" window — reads the editor's `RingBufferSink` through a
`LogFilter`: level and category toggles with per-level counts, a text search, Copy (the filtered rows through
`formatEntry` to the clipboard), Clear (remembers a sequence number rather than mutating the shared sink; the
rows and per-level counts come from the headless `ConsoleView.h`), and Auto-scroll; entries the connected server
forwards land in this same sink, message-prefixed `"[server] "`, so
they show and filter alongside the editor's own — see [logging.md](logging.md)), `AssetBrowserPanel` (its type
filter is a union of toggled chips, with an "All" chip to clear: `AssetFilter.h`), `AssetDisplay` (shared asset
label/name/icon/color rules, header-only), `PlayControls.h` (the headless play/pause/stop enable rules for the
Scene Status toolbar's fixed row of icon buttons, which stay drawn and gray out when unavailable), `SaveUI`
(file I/O on the project folder layout, `<folder>/project.ecs3d` beside `assets/` and `scripts/`: Save As picks
a folder, creates the subfolders, copies the previous folder's `assets/` and `scripts/` when the folder differs,
and moves the editor's project root there, refusing a folder that already holds another project; Open takes a
`.ecs3d` or legacy `.json` file, or a dropped folder, through `locateProject`; a legacy `.json` is saved in
place until Save As; a template (see Build System's project root) is titled "Untitled (from" plus its name in
the window title and its Save acts as Save As into a free or empty folder, prefilled under `Documents/ECS3D
Projects`, refusing the template's own folder or one inside it (`ProjectOrigin.h`); the Assets panel's import
and new-script actions ask `SaveUI::blockAssetWrites` first; every load and a root change reach
`GpuAssetCache::setProjectRoot` through a callback; also owns the unsaved-changes gate - the edit counter and
the perform/ignore/prompt decision are the headless `UnsavedChanges.h`: New/Open/a dropped file/closing the
window all route through a guard that prompts Save/Don't Save/Cancel when a send-path callback has marked the
project dirty since the last save/load - the window-close intercept sets its own raw
`glfwSetWindowCloseCallback`, since `vke::Window` sets none; each successful open and save is added to the
shared recent-projects list through `RecentProjectRecording.h`, except an untitled template, which waits for its
first save to a folder; `forgetSaveTarget` drops the save file and folder when `ProjectLoadTracker.h` sees
another editor's load), `GuiComponents` (whose numeric widgets refuse a non-finite ctrl-click entry, restoring
the previous value and logging a warning, since a non-finite float serializes as json null), `Gizmo` (the
headless translate/rotate/scale manipulator core - hit-testing and drag math over world-space line/triangle
primitives it hands back for a backend to draw; pure glm, no ImGui/Vulkan, so it compiles straight into
`ECS3DTests`), `GizmoRenderer` (the backend-neutral drawing interface `Gizmo.h`'s output is drawn through) /
`ImGuiGizmoRenderer` (its only implementation today, drawing into the scene overlay's `ImDrawList`),
`ViewportGizmo` (owns the `gizmo::State` and renderer for the current selection, reachable from `EditorApp`'s
scene overlay callback - see [viewport-gizmos.md](viewport-gizmos.md)). The convex mesh collider handler (in
`ColliderEditor`) takes a source model through the shared `AssetSlot`. Picking one, or pressing Rebuild hull,
rebuilds the hull in the editor from the first mesh of the model in `GpuAssetCache`. The point-to-hull step is
the headless `ConvexHullRebuild`. A failure, or a rebuild that changes nothing, logs at most a warning and
reports no edit. Edits are whole-component, so every convex collider edit carries its whole hull, up to about 9
kilobytes. In a multi-object selection the hull keys `vertices`, `indices` and `sourceModel` travel together
(`componentFieldDelta::withLinkedKeys`), a failing per-object apply is rolled back and skipped with one warning
per gesture, and Rebuild hull is disabled while those keys differ. A model drop on such a selection still sends
the edit, and an edit that leaves the primary unchanged propagates only its disagreeing hull keys. Depends on
`ECS3DData` + `ECS3DRender` + `ECS3DSettings` + `nfd`.
