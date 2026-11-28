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
built on a stock widget (`accentSlider` draws its value, or the mixed dash, in the primary text color in a
fixed-width column right of the track, laid out by the headless `AccentSliderLayout.h`); a residual "Mixed
values" line still names the few fields with no per-widget indicator (an asset reference slot, the collider mask popup, a non-numeric script field), and each object's edit still
goes out and is recorded through the normal single-object path, so undo records one entry per object; Add/Remove
Component and the name field are left out of this view for now, since wiring them up would mean deciding which
object they act on) and the asset kind to `AssetInspector` (per-`AssetType` views — read-only detail plus a
display-name rename field (a scene's name field commits once, when editing ends) and a delete button with a
reference-count warning for the flat file assets (it counts objects whose components or script fields mention the
uuid, in scenes and prefab bodies, plus scene scripts); the
**Prefab body is editable** — deserialized into a detached `TransientObject` and edited by a reused
`ObjectInspector`, see [prefabs.md](prefabs.md)), `EditorSelection` (shared kind-tagged selection slot holding
an ordered set of uuids rather than a single one, `Selection.h` - the back of the list is the primary, what the
Inspector/gizmo show; in the tree, a plain click replaces it, Ctrl-click toggles membership, and Shift-click
selects the contiguous range between an anchor uuid and the clicked row in the tree's current on-screen order,
which `ObjectGUIManager` tracks itself each frame since collapsed nodes and the sort mode both affect what "on
screen order" means; Ctrl+A in the focused panel selects the whole scene depth-first in the current sort mode, and
the header shows the selected count unless the panel is too narrow to fit it beside the label; Ctrl+A is
panel-local, not a `KeybindTable` action; the range, row-action targets, depth-first order, count label and
sort rules live in the headless `ObjectTreeOrder.h`; the viewport has its own rule, described under Viewport
gizmos below), `SettingsPanel` (the "Settings" window — a
section nav beside the selected section's content, opening centered at a font-relative default size on first use
(a saved panel layout wins; `SettingsPanelLayout.h` holds that math) with the Keybinds columns sized from
their content, reading and writing `ECS3DSettings` directly since
preferences are local, not replicated; Appearance edits the `EditorTheme.h` palette tokens live; Viewport drags
the free-fly camera speed (`EditorCameraSettings`, applied to `vke::Camera::setSpeed` immediately) and the
viewport gizmo's Snap toggle + translate/rotate/scale step sizes (`EditorGizmoSettings`); the terrain brush
preferences (`EditorTerrainSettings`, no panel yet) are described in
[library-reference.md](library-reference.md); Display holds vsync and the
frame rate limit (`editor.display.vsync`/`editor.display.maxFps`, applied at renderer creation and live through
`RenderingManager`; the numeric field commits on deactivation only, and `FramePacingNotes.h` decides the
explanatory notes; its Reset to defaults covers frame pacing only), then a Rendering block with the Ray tracing checkbox (`editor.display.rayTracing`, default on like
the engine's; `EditorViewportSettings.h` holds the read/write helpers and `rayTracingAtStartup`, which keeps a stored
true off on a device that cannot ray trace; applied after renderer creation and live, greyed out when unsupported);
Keybinds lists every
`EditorAction`, its current chord, and Rebind/Unbind/Reset controls, driving `KeybindDispatcher`'s capture mode
and showing a conflict modal - naming the holding action, no reassign option - when a rebind targets an
already-held chord; `KeybindRebindFlow::reconcileCapture` keeps the flow and the dispatcher's capture in
agreement, and both sides are reconciled before each frame ends (`KeybindCaptureFrame.h`; `abandonKeyCapture()` covers
a hidden GUI), so Cancel, Unbind, Reset, Reset all, closing the window, switching sections or Escape end both),
`KeybindDispatcher` (the one `vke::KeyCallbackEvent` listener that resolves a press against a `KeybindTable` and
calls the registered handler, suppressed while ImGui wants the keyboard; also drives the
Settings panel's rebind capture; actions registered with `KeyHold::repeatWhileHeld` (undo, redo and duplicate,
chosen by `defaultKeyHold`) fire again while their chord stays held, after `keyHoldInitialDelay` and then every
`keyHoldRepeatInterval`, driven by the
headless `KeyHoldRepeat.h` and polled by `KeybindDispatcher::update()` once per frame; a handler with nothing
left to do calls `stopRepeat()`), `SceneScriptsPanel` (the "Scene Scripts" window: add, remove, reorder and edit
the current scene's scripts, docked on the Inspector's side; its pure decisions are the headless
`SceneScriptsPanelLogic.h`, and the field widgets are `ScriptEditor.h`'s `drawScriptFieldWidgets`, shared with
the object Script handler), `ConsolePanel` (the "Console" window — reads the editor's `RingBufferSink` through a
`LogFilter`. The top line holds a Filters button, the search field and, when they fit, Copy, Clear and Auto-scroll;
`ConsolePanelLayout.h` decides. Below it are the level toggles with per-level counts and the category toggles, which
the Filters button hides. Copy (the filtered rows through
`formatEntry` to the clipboard), Clear (remembers a sequence number rather than mutating the shared sink; the
rows and per-level counts come from the headless `ConsoleView.h`), and Auto-scroll; entries the connected server
forwards land in this same sink, message-prefixed `"[server] "`, so
they show and filter alongside the editor's own — see [logging.md](logging.md)), `ProfilerPanel` (the "Profiler"
window, closed by default, F3 or Window, then Profiler: the editor's frame lane as stacked bars from `ECS3DRender`'s
`ProfilerCharts`, a legend, and a per-scope breakdown. Pause copies the lane's history, clicking a bar selects that
frame and pauses in the same frame, and closing the panel resumes and drops the copy. The window, scale and hidden
groups persist in
`editor.profiler.window`, `editor.profiler.scale` and `editor.profiler.hiddenGroups`; the choices, their validation
and the pause/selection rules are the headless `ProfilerPanelState.h`. Below the editor lane sits the server's tick
lane (own legend, own `editor.profiler.serverHiddenGroups`, a 20 ms budget line) and a collapsing Network section
(the server's and the editor's own traffic, in bytes and messages per second).
`EditorApp` (`EditorAppProfiler.cpp`) subscribes the connection to the server's statistics only while the panel is
open or the corner readout is on (`profilerPanelState::wantsServerStats`) and fills a `ServerLaneInput`
(`ProfilerServerLane.h`, plain data, so the library needs no `ECS3DNet`) from
`net::RemoteStats` each frame; the panel reads it through `setServerLane`. Pause freezes both lanes, each bar click
selects in its own lane, and the Breakdown table shows the lane clicked last. The stats readout,
`editor.viewport.statsReadout` (off by default;
a checkbox in Settings > Viewport and "Corner readout" in the panel header), is drawn by
`EditorApp::drawStatsReadout` in the scene overlay callback onto the scene view's draw list, so it survives F10
hiding the GUI; its three lines are the headless `ProfilerReadout.h`). "Save capture..." writes the shown window
through `ProfilerCapture.h` (see [profiling.md](profiling.md)) and the panel then shows the path with an "Open folder"
button (`OpenFolder.h`, which launches the platform file browser without a shell), `AssetBrowserPanel` (its type
filter is a union of toggled chips, with an "All" chip to clear: `AssetFilter.h`), `AssetDisplay` (shared asset
label/name/icon/color rules, header-only), `PlayControls.h` (the headless play/pause/stop enable rules for the
Scene Status toolbar's icon buttons, which stay drawn and gray out when unavailable), `SceneStatusLayout.h`
(the toolbar wraps its control groups onto more rows when the panel is too narrow and raises the docked panel's
height floor to fit), `SaveUI`
(file I/O on the project folder layout, `<folder>/project.ecs3d` beside `assets/` and `scripts/`: Save As picks
a folder, creates the subfolders, copies the previous folder's `assets/` and `scripts/` when the folder differs,
and moves the editor's project root there; every save first runs the app's `ContentWriter` on the target folder,
which writes the terrain files (`TerrainSaving.h`, see [project-layout.md](project-layout.md)) and fails the save
when it returns false, refusing a folder that already holds another project; Open takes a
`.ecs3d` or legacy `.json` file, or a dropped folder, through `locateProject`; a legacy `.json` is saved in
place until Save As; a template (see Build System's project root) is titled "Untitled (from" plus its name in
the window title and its Save acts as Save As into a free or empty folder, prefilled under `Documents/ECS3D
Projects`, refusing the template's own folder or one inside it (`ProjectOrigin.h`); the Assets panel's import
and new-script actions ask `SaveUI::blockAssetWrites` first; every load and a root change reach
`GpuAssetCache::setProjectRoot` through a callback; also owns the unsaved-changes gate - the save-point
comparison and the perform/ignore/prompt decision are the headless `UnsavedChanges.h` (see
[undo-redo.md](undo-redo.md)): New/Open/a dropped file/closing the window all route through a guard that
prompts Save/Don't Save/Cancel when the project differs from the last save/load - the window-close intercept
sets its own raw `glfwSetWindowCloseCallback`, since `vke::Window` sets none; each successful open and save is
added to the shared recent-projects list through `RecentProjectRecording.h`, except an untitled template, which
waits for its first save to a folder; `forgetSaveTarget` drops the save file and folder when `ProjectLoadTracker.h` sees
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

The Terrain component section also shows read-only `Chunks` (held against the count the snapshot announced) and
`Surface` (none, ready, percent pending, growing, too large to draw or to grow) rows, formatted by the headless
`TerrainReadout.h` from `GpuAssetCache::terrainSurfaceStatus` and the terrain store.

The Assets panel's Create items include "New Terrain", which opens its own modal (less invasive than extending
`AssetCreateFlow`'s name-only one). The form takes a name, a Flat or Heightmap shape, a voxel size in meters and, for
Flat, a footprint in meters. A heightmap adds an image picker and a height scale in meters, and its footprint is the
image size times the voxel size. Sizes are converted at `worldUnitsPerMeter` (`WorldUnits.h`, which
`PhysicsSystem::unitsPerMeter` reads). Enter and Escape act on the form only when no numeric field is being edited
or the name field is.

The headless `TerrainCreateFlow.h` holds the request, its validation, the heightmap decode and the generated volume:

- Validation checks a free terrain display name, finite positive numbers and the voxel size inside
  `minVoxelSize`/`maxVoxelSize` after conversion. It also checks a chunk estimate within `maxCreateChunks` (4096),
  before anything is allocated. Generation runs on the frame Create is pressed, so the cap sits well under the
  server's `maxEditChunks` and `maxAddedTerrainChunks` bounds, which stay. The image sample and file-size limits
  derive from the same cap, and the form shows the estimated chunks and a memory ceiling.
- The decode uses `stbi_load_16` forced to one channel, with 8-bit scaled by 257. A picked path is UTF-8
  (`pathFromUtf8`).
- The volume is `fillFlat` over a few voxels of solid, or `fillFromHeightmap`, and the description holds one palette entry, "Ground", a flat gray (0.6) without textures.

Create sends the terrain through `EditorApp::onCreateTerrain`:

- The addAsset built by `terrain::addTerrainAssetJson` (plus the display name) goes through `addAsset`, so the
  editor's own store gets the empty volume. A local refusal sends nothing, and the form is not editable while the
  server is not.
- The generated chunks are restored into the store and queued on `m_terrainUpload` for the server.
- The asset is recorded for undo once the chunks are in the store (`commandForTerrainAdd`), so redo can upload them again.
- A `terrainCreate::placementEdit` batch (a root object with a Terrain component) goes through `onSceneEdit`, so it is
  recorded for undo as the next entry. Ctrl+Z removes the object first and the terrain asset second; see
  [undo-redo.md](undo-redo.md#terrain-assets).
- The object is selected once the server's rebroadcast brings it in, waiting up to 10 s, and the pending selection is
  dropped if the user selects something else first.

A queued upload whose terrain a snapshot no longer holds (the server refused it) is dropped with a warning
(`terrain::forgetUnknownTerrains`). Two cases keep it: a terrain created within a few seconds, since an older
snapshot does not list it yet, and any snapshot sent before the server applied this editor's own project load
(`ProjectLoadTracker::snapshotIsCurrent`), which still lists the old project. Another editor's project load clears
the queue. On an untitled template the menu item calls `SaveUI::blockAssetWrites` instead
(`TerrainCreateFlow::gate`), since the terrain would have no folder to be saved into. A generation or decode error
shows in the form and leaves nothing created.

Terrain mode is the viewport brush for the one selected terrain object. `EditorApp` owns a `TerrainToolState` and
builds its `TerrainModeContext` every frame: exactly one selected object whose Terrain collider names a volume in
`m_terrainStore`, `m_serverEditable`, and a stopped scene. `refreshTerrainMode` runs after the network step, so a
change of selection, a lost connection, a started Play or a volume that vanished leaves the mode and ends any stroke.
The mode is entered from the Sculpt checkbox the Terrain section draws (`registerColliderEditors` takes a
`drawSculpt` callback, and `EditorApp::drawTerrainSculpt` draws it only for the selected object's own component, so a
prefab body shows none) or with the `terrainMode` keybind. While it is on, the section shows a radio button per tool
(Add, Subtract, Smooth, Flatten, Paint) and Radius, Strength and Falloff, all written through
`editorTerrainSettings::writeBrush` for the current tool (Paint hides Strength and Falloff, and its template
forces strength 1 and falloff 0). Add, Flatten and Paint also show a Material row of color swatches, one per entry of the terrain's
palette (the eight renderer default colors when it has none; `selectableMaterials`) that sets `TerrainToolState::activeMaterial`,
a session choice that `clampMaterial` bounds when the stroke template is built. The row wraps like the tool buttons,
the active swatch has an accent outline and the tooltip is the `materialLabel`. `materialSwatch` alone decides a
swatch: the renderer's default color (wrapped by index) for an empty palette, an entry's flat color, or its albedo
texture, which `EditorApp::drawTerrainMaterialSwatch` draws as an `ImGui::ImageButton` of the thumbnail
`textureThumbnail` (`components/AssetSlot.h`) gets from `GpuAssetCache`, falling back to a neutral gray while the
thumbnail is not available. Add and Flatten write
the material into the ground they raise and Paint into existing ground. A Paint stroke changes materials only, so
`strokeRedistances` makes its release send and predict no redistance, and `strokeAim` keeps its cursor on the starting
surface however far it jumps (the upright-plane fallback would land off the ground, where it paints nothing). The tool
radio buttons wrap onto further rows when the section is narrow (`buttonRowBreaks`).
The tool keybinds only switch the tool (`writeLastTool`) and never enter the mode, and the radius keybinds scale the current tool's radius by 1.25 or 0.8. A brush
radius above `maxBrushRadiusVoxels` voxels is clamped (`effectiveBrushRadius`) when a stroke begins and when the ring is
drawn, since the server refuses a larger brush. The headless parts are `TerrainStroke.h` (spacing along the pointer
path), `TerrainBrushSession.h` and `TerrainCursor.h`; the viewport side is in
[viewport-gizmos.md](viewport-gizmos.md). A finished stroke is one undo step ([undo-redo.md](undo-redo.md)), and the
unsaved flag is held for the length of a stroke. A new brush starts on material 0.

The Terrain section also draws a Materials block, through a second `registerColliderEditors` callback
(`drawMaterials`, `EditorApp::drawTerrainMaterials`, in `EditorAppTerrainPalette.cpp`), whether or not Sculpt is on. It is
an `ImGui::CollapsingHeader`, open by default, holding a bordered list of one line per palette entry (its swatch and
its `materialLabel` as a Selectable) whose height `TerrainPaletteEditor::listHeight` caps at `maxVisibleRows` (8) lines,
scrolling beyond. The selected entry is `TerrainToolState::activeMaterial` (clamped), so clicking a line also picks the
paint material, and the toolbar swatches move the selection too. Add material sits under the list and selects the new
entry. Only the selected entry's details are drawn below it: a name field, a Color or Texture choice, a specular
texture slot (`drawAssetSlot`), Move up, Move down and Remove; Add material is disabled at `terrain::maxPaletteSize`
entries. The headless `TerrainPaletteEditor.h` turns each intent into a new palette or a
refusal string with the `TerrainPalette.h` functions (a new entry is "Material N"; Remove scans `materialsInUse` only
when clicked) and gates editing on an editable connection, a stopped scene and a terrain the store holds: a palette edit
during Play would outlive Stop on the server while the play history is discarded. The gate's reason shows under the
block; an intent's refusal is a `MaterialNote` (`TerrainMaterialPrompts.h`) raised on the material the edit was about,
shown beside Remove in the secondary text color, and cleared once a frame draws another material or terrain. After an
applied move or remove of the shown terrain `selectionToFollow` keeps a moved entry selected and hands a removed one's
selection to its neighbor; no other edit touches the selection. The frame Add material applies skips the details, so a
pending name or color settles the next frame through `endFrame` rather than being dropped.

Remove and the moves keep painted ground looking the same (`EditorAppTerrainMaterialRemove.cpp`).
`TerrainPaletteEditor::remove` and `move` always return, beside the palette, the `remapForRemoval` or `remapForMove`
table: `materialsInUse` sees only solid samples, and air samples that a later Add exposes must shift too. Both are
refused by `TerrainPaletteEditor::remapRefusal` unless the ordinary gate passes, the view has received the whole terrain
once (`TerrainViewSync::hasWholeTerrain`: its volume reached the description's `chunkCount`, so `materialsInUse` and
the undo capture see every chunk; carving chunks to air afterward does not undo it, and a new load or a replaced volume
does) and no
brush stroke is active or awaiting its final chunks. On the click the editor scans `materialsInUse`, and removing an
entry solid ground paints first opens a `MaterialRemovePrompt` modal listing the other
materials (swatch and label) for what its ground becomes, defaulting to `defaultReplacement` (the previous entry, or the
next one for entry 0); the prompt closes without acting if the palette changes underneath it or a frame does not draw
the Materials block (`endFrame`). The last material is never removed: a terrain keeps at least one.
`finishTerrainPaletteEdit` sends a removal or move through `sendTerrainRemap`, which predicts the remap and the new
palette with `edits::predictTerrainRemap` (refusing a palette that is invalid or unchanged before anything is sent),
sends the one `remapMaterials` terrainEdit and records its one undo entry ([undo-redo.md](undo-redo.md)); no separate
palette addAsset is sent. The data side keeps `terrain::removeMaterial`, which refuses a painted
entry, and adds `eraseMaterial` for the remapped path.
`EditorApp::applyTerrainPalette` builds the command with `commandForTerrainPalette` from the pre-edit store, applies
the replacement through `applyAddAssetWithTerrain` (no upload expected), sends the addAsset and records the command.
Every intent is built from the palette the store holds when it runs, and once one edit has applied in a frame (Add
material, or one in the details) no other runs that frame, so two edits never race on a stale copy. Undo and redo
apply the palette payload locally first ([undo-redo.md](undo-redo.md)).

A name or a dragged color is a `PendingField` (`MaterialNameField`, `MaterialColorField`): it commits one palette edit
when it settles (the field is left, the picker's own widget is released), compares colors with a small tolerance, and
belongs to the material it started on. It settles as soon as a frame draws without its details (another terrain,
another material selected, the block collapsed), and it is dropped when the material at its index is no longer the one
it started on.

A material is a flat Color while its albedo texture is nil (derived, not stored): Color shows an `ImGui::ColorEdit3` and
Texture shows the albedo slot. Switching to Color clears the albedo and keeps the color and specular. A row switched to
Texture before a texture is picked is held by `TextureModeRows`: it follows its material through the block's own moves
and removes, ends when the row gets an albedo, and is dropped when the palette changes any other way (undo, redo,
snapshot) or another terrain is shown. New entries take `vke::s_defaultVolumeColors` by index, and the swatches in the
list and the toolbar show an entry's color or its albedo texture's thumbnail. A terrain created
from the asset browser starts with one gray "Ground" entry without textures.

The panel layout (window placement, detached panels and the dock tree) is a user preference, not project data.
`EditorApp` runs ImGui without an ini file and keeps the ini text in `ECS3DSettings` under `editor.layout.imgui`
(`EditorLayoutSettings`). It restores the text in the style-setup hook, before the first frame, and saves it when
ImGui sets `WantSaveIniSettings` and again on shutdown. When the saved text holds a dock tree
(`editorLayoutSettings::hasDockLayout`), `updateDockSpace` skips the default dock percents and locations and applies
the per-panel minimum sizes and the resize fit; with no saved layout the defaults apply as on a first run.
`saveLayout` never replaces a stored dock tree with text that has none (`shouldReplaceLayout`). A panel missing from a
restored layout, such as one added in a later build, opens floating rather than in its default dock; the per-panel
minimum sizes still apply.

The stored layout carries a version (`editor.layout.version`, `editorLayoutSettings::currentLayoutVersion`). A layout
saved by another version, or with none, is not restored, so the current defaults apply as on a first run, and the
next save replaces it. Bump the version whenever the default layout geometry changes, or users keep the old
proportions.

ImGui keeps a docked side node at its absolute `SizeRef` when the window shrinks and gives the central node what is
left, so a narrow window squeezed the scene view. `EditorApp::fitDockToViewport` (the headless `DockLayoutFit.h`)
remembers the fraction of the dock space each side node takes and, when the dock space size changes, writes `SizeRef`
from those fractions again, shrunk together on an axis so the central node keeps at least 40%. A side node is a
visible child whose sibling holds the central node; a closed panel passes the walk on to the visible child and keeps
its remembered share while its node exists, and the Scene Status node is left out because `displaySceneStatus` sizes
it to its content, but only as a leaf of its own in a vertical stack (tabbed with other panels or docked in a side
column it is an ordinary node); when its row count drops, the node follows the floor down only if it still sits at the
previous floor, and never during a drag. The fractions are the memory, so shrinking and growing back restores the
layout, and a hand-resized layout keeps its own proportions. They are remembered from the rebuilt default tree (the
engine rebuilds it with the same node ids, so three settle frames re-read it) and from a restored one. A drag in the
dockspace updates only the nodes that moved more than a pixel since it began, so a click on a tab or splitter changes
nothing, and a docking change adopts only new nodes. A zero-size (minimized) frame changes nothing. The side and
bottom panel floors are re-applied whenever the viewport size or content scale changes, at most 16% (width) and 17%
(height) of the window, so two side floors leave the central node at least 68% of the width; where a floor is larger than
the fit, the floor wins. The change takes effect one frame after the resize.

The default layout is sized by the headless `DefaultDockLayout.h`. Objects take 258 and Inspector 288 unscaled
pixels, times the content scale, and together at most 37% of the width. The bottom dock takes 220 unscaled pixels,
which shows the Assets search row, filter row and one row of tiles, and at most 26% of the height. At 2000x1087 and
scale 1.25 that is about 16% (Objects), 65% (scene view) and 18% (Inspector) of the width, and 25% of the height for
the bottom dock. The panel floors (the same constants the app applies) are 245 (Objects), 285 (Inspector, Scene
Scripts) and 150 (Assets) unscaled pixels, capped at 16% of the width for side panels and 17% of the height for
bottom panels. Small windows give up space gracefully: the scene view keeps at least 60% of the width, and 60% of the
height in a roomy window (at least 50% under 700 unscaled pixels tall, 45% under 400) with the Scene Status toolbar
on one row. A toolbar that wraps onto more rows takes more.

`DockLayoutState` holds the remembered proportions and decides each frame what to write back. A layout still at its
defaults follows a new window size, or a new content scale at the same size, to the default pixel sizes, including a
resize during the settle frames. Only the end of a drag that moved or added a node ends that, so the engine's own
startup rebuilds never do. After that, the remembered fractions are rescaled as described above, and a content scale
change at the same window size leaves them alone: they are fractions of the window.

## Connection state

`EditorConnectionState.h` names why the editor cannot edit, apart from the `m_serverEditable` gate: `connected`,
`readOnlyServer` (the server's `editStatus` said it is not in edit mode), `connectionLost` and `editTokenRejected`.
It also holds the menu-bar badge, the Objects panel notice (`ObjectGUIManager::setReadOnlyNotice`) and the log line
for each state. `EditorConnectionTracker` is the state machine `EditorApp` drives from its network loop.

The transport carries no close reason, so rejection is inferred. A server that accepts the editor's join replies with
an edit status and then a snapshot. A rejecting server closes the socket silently
(`TransportBackend.TryAuthorizeHandshake`), with or without a token having been given. So a loss before any server
message, on a server the editor did not spawn (`--host`), is `editTokenRejected`. A server the editor spawned uses a
token the editor generated, so that drop is `connectionLost`, as is any drop after the first message. A server that
refuses the editor for another reason reads as a rejection too, which the wording admits.

On a loss the loop applies the messages still queued before it classifies, so a server that answered and then died
reads as lost. After a loss the tracker ignores a later `editStatus`, and the editor stays read-only.
