# Editor Basics

This guide covers the ECS3D editor: where things are, how to build a scene, and how to try it out. It
assumes the editor is built and running. The editor starts its own edit server, so every change you make is
a command sent to that server, which owns the scene.

If the menu bar shows a "READ-ONLY (server not in edit mode)" badge, you are attached to a server that does
not allow edits, and the editing controls are disabled.

## The Objects panel

The "Objects" panel is the hierarchy: every object in the current scene, as a tree.

- Create a root object with the Create New Object button at the top.
- Each row has a plus button (add a child) and a minus button (delete the object).
- Right-click a row for Add Child, Duplicate, Save as Prefab and Delete.
- Drag a row onto another row to make it a child of that row. Drop it on empty space to move it to the root.
  The thin drop zones between rows place it at that position among its siblings.
- The Sort control switches between Authored Order and Alphabetical. This only changes how the tree is
  shown.
- A plain click selects one object. Ctrl+click adds or removes one object from the selection. Shift+click
  selects every row between the last clicked row and this one, as the tree is currently displayed.
- With two or more objects selected, Delete and Ctrl+D act on all of them as a single undoable step.

Renaming is done in the Inspector (below), not in the tree. Moving an object under a parent keeps it in the
same place in the world.

## The Inspector

The "Inspector" shows the selected object: a Name field, its components, and its scripts.

- Edit a component by changing its widgets. Dragging a slider is one undo step, not one per frame.
- Add Component lists Rigid Body, Model Renderer, Light Renderer, Box Collider, Sphere Collider, Capsule
  Collider, Convex Mesh Collider, Player Controller and Camera. A type the object already has is hidden.
  An object has at most one collider, so once it has one, the other collider kinds are hidden too.
  Every object has a Transform, which cannot be added or removed.
- Scripts are not in that list. Drag a script asset from the Assets panel onto the Inspector's Scripts
  area. See [Your first script](your-first-script.md).
- With several objects selected, the Inspector shows the components they share. Editing a field changes
  only that field on every selected object. A field whose values differ shows a mixed state (a dash).
  Add Component and the Name field are not available in that view.

## The viewport

The scene window is the viewport. By default it looks through the Editor Camera, a free-fly camera.

- Hold the right mouse button over the viewport and move the mouse to look around. The W, A, S and D keys
  move the camera.
- Camera speed is a slider in Settings, under Viewport (range 0.1 to 20, default 1).
- Ctrl+click an object to select it, or Ctrl+click empty space to clear the selection. Ctrl+Shift+click
  toggles the object in or out of the current selection. A plain click only focuses the viewport or grabs a
  gizmo handle.
- Lights show a small icon at their position while the scene is stopped. Ctrl+click it to select the light.

### The transform gizmo

The selected object shows a gizmo for moving, rotating or scaling it.

- Switch modes with the Move, Rotate and Scale buttons in the Scene Status panel, or with the keys 1, 2
  and 3 (the default keybinds, listed under Settings below).
- The Local checkbox in Scene Status picks the object's own axes instead of world axes. It is disabled in
  Scale mode, which always uses the object's own axes.
- Snapping is set in Settings, under Viewport: a Snap checkbox (on by default) and step sizes for
  translate (0.5), rotate (15 degrees) and scale (0.1).
- Dragging a handle sends updates while you drag, so other connected views see it move. Releasing the
  handle records one undo step.

## Scene Status

The "Scene Status" panel along the top holds the play controls and some view choices.

- The play, pause and stop icon buttons. Play also resumes a paused scene. Buttons that do not apply are
  grayed out.
- A readout of the scene name and its state: Stopped, Running or Paused.
- A Ray Tracing checkbox. This is a local rendering setting, grayed out if the device cannot ray trace.
- The View combo chooses what the viewport looks through: Editor Camera, or any object in the scene that has
  a Camera component. A player's camera is labeled with its Player Controller slot. If the chosen object goes
  away, the view falls back to the Editor Camera.
- The Player combo chooses which player slot receives your keyboard and mouse input while you play. Picking
  another slot also points the View at that player's camera, if it has one.

A scene always starts Stopped after it is loaded. Scripts and physics run only while it is Running.

**Stop discards every runtime change.** Objects spawned by scripts, objects destroyed, and any edit you made
while playing are thrown away, and the scene returns to what you authored before pressing Play.

## Undo and redo

Ctrl+Z undoes and Ctrl+Shift+Z redoes. Holding the keys repeats the step. The Edit menu has the same two
items and names the action they would take, for example "Undo Rename Cube". Undo sends a normal edit to the
server, so every connected view stays in sync. If something else changed the target in the meantime, the
undo is refused and the log names the object that conflicted.

History is cleared when you open or create a project, switch scenes, or reconnect. Pressing Play starts a
separate history for the play session, and Stop returns you to your authored history.

## The Assets panel

The "Assets" panel shows models, textures, scripts, prefabs and scenes. It has a search box, a sort combo
and type filter chips.

- The plus button, the right-click menu on empty space, and the Assets menu in the menu bar create assets:
  New Scene, New Prefab and New Script. The Assets menu also has Import Model and Import Texture, which
  open a file dialog.
- Double-click a scene to make it the active scene. You can also drag a scene onto the viewport.
- Drag a model or texture onto a matching slot in the Inspector. Drag a prefab onto the Objects panel to
  instantiate it, or onto an object row to instantiate it as a child.
- Selecting an asset shows it in the Inspector. Models, textures, scripts and prefabs can be renamed
  there, which changes only the display name. A scene can be renamed too, which changes the scene's real name;
  a name another scene or prefab already uses is refused. Prefab contents are editable in place.

## The Console panel

Open it from Window, then Console. It shows the editor's log and the server's log in one list. Lines from
the server are prefixed with "[server] ". You can:

- toggle each level (trace, debug, info, warn, error, each with a count) and each category;
- search the text;
- Copy the filtered lines to the clipboard, Clear the list, and turn Auto-scroll on or off.

Output from a script's `Log.info` and similar calls appears here under the script category. Compile errors
in your scripts appear here too.

## Settings

Open Window, then Settings. It has three sections.

- Appearance: the editor's colors.
- Viewport: camera speed, Show Grid and the gizmo snap options. The grid shows only through the Editor Camera.
- Keybinds: every action with its current key, plus Rebind, Unbind and Reset buttons, and Reset all to
  defaults. Rebinding to a key another action already uses is refused and names that action.

Settings belong to you and your machine. They are not saved in the project.

The default keybinds are:

| Action | Default |
|--------|---------|
| Toggle GUI | F10 |
| Save Project | Ctrl+S |
| Save Project As | Ctrl+Shift+S |
| Undo | Ctrl+Z |
| Redo | Ctrl+Shift+Z |
| Delete Selection | Delete |
| Duplicate Selection | Ctrl+D |
| Focus Selection | F |
| Gizmo: Translate | 1 |
| Gizmo: Rotate | 2 |
| Gizmo: Scale | 3 |

Focus Selection can be bound, but it has no behavior yet. Keybinds are ignored while a text field wants
keyboard input.

## Saving and opening

- File, then Save (Ctrl+S) writes the project to disk. Save As... (Ctrl+Shift+S) picks a new location.
- File, then New and File, then Open replace the whole project. Dropping a project file on the window opens
  it too.
- If you have unsaved edits, New, Open and closing the window ask whether to Save, Don't Save or Cancel.
