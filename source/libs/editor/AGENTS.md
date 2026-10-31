# AGENTS.md - ECS3DEditorLib

Agents load this file when they read a file in `source/libs/editor/`. It holds what a change here must keep. The full
notes are in [docs/architecture/editor.md](../../../docs/architecture/editor.md).

Related design notes: [undo-redo.md](../../../docs/architecture/undo-redo.md),
[viewport-gizmos.md](../../../docs/architecture/viewport-gizmos.md),
[prefabs.md](../../../docs/architecture/prefabs.md),
[camera-and-input.md](../../../docs/architecture/camera-and-input.md).

- The authoritative scene lives on the edit server. An edit becomes a command sent to it, not a local mutation, and
  the editor's view updates when the server rebroadcasts. Asset mutations are the exception: they apply locally first,
  then send.
- `EditorApp` records every mutation's undo command from the pre-edit replicated view before it sends
  (`edits/RecordEdits.h` in `ECS3DData`). A new mutation path does the same.
- A continuous edit, such as a slider drag, sends as it moves but records one before/after pair once no widget is
  active. A prefab body edit flushes to the network only then, since each flush re-snapshots the whole project.
- Keep decisions in headless headers (`ObjectTreeOrder.h`, `PlayControls.h`, `UnsavedChanges.h`, `UndoRequestGate.h`,
  `Gizmo.h`, `AssetFilter.h`, `ObjectNameField.h`, `SettingsPanelLayout.h`, `DashedBoxLayout.h`,
  `ConsolePanelLayout.h`, `ConsoleView.h`, `GizmoEditSending.h`, `PrefabBodyEdits.h`, `KeybindRebindFlow.h`,
  `AssetCreateFlow.h`) so the test suite compiles them without ImGui. The ImGui code draws.
- Numeric widgets come from `GuiComponents`, which refuse a non-finite entry.
- Preferences read and write `ECS3DSettings` directly; they are local, not replicated.
- A file that uses ImGui internals such as `ImGui::GetActiveID` includes `<imgui_internal.h>` itself. MSVC compiles
  without it through a transitive include, but gcc and clang do not.
