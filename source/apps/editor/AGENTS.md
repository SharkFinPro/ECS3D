# AGENTS.md - ECS3DEditor

Agents load this file when they read a file in `source/apps/editor/`. It holds what a change here must keep. The
full notes are in [docs/architecture/apps.md](../../../docs/architecture/apps.md),
[docs/architecture/undo-redo.md](../../../docs/architecture/undo-redo.md),
[docs/architecture/camera-and-input.md](../../../docs/architecture/camera-and-input.md) and
[docs/architecture/viewport-gizmos.md](../../../docs/architecture/viewport-gizmos.md).

- `EditorApp` is a client: the authoritative scene lives on its `--edit` server, so each mutation callback sends a
  command and waits for the rebroadcast.
- Each mutation callback asks `RecordEdits.h` for its undo command before it sends, while the replicated view still
  holds the before state. Undo and redo requests are paced by `UndoRequestGate.h` until the server's rebroadcast
  lands.
- Do not gate forwarded mouse input on `io.WantCaptureMouse`. The 3D viewport is itself an ImGui window, so that flag
  is set whenever the cursor is over the scene. `captureGatedInput` forwards the mouse only while the viewport looks
  through a scene camera and the scene is focused.
- Picking runs from `run()` after `sendInput()`, not from the scene overlay, because the pick flags are set only
  inside `render()`. `ViewportGizmo::capturesMouseAt` answers whether a click lands on a gizmo handle.
- `setupImGuiStyle` sets `ConfigWindowsMoveFromTitleBarOnly`, so a drag inside a detached scene window drives the
  gizmo instead of moving the window.
