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
- Ctrl+A, while the Objects panel is the focused window, selects every object in the scene.
- The panel header shows how many objects are selected, beside the total object count.

Renaming is done in the Inspector (below), not in the tree. Moving an object under a parent keeps it in the
same place in the world.

## The Inspector

The "Inspector" shows the selected object: a Name field, its components, and its scripts.

- Edit a component by changing its widgets. Dragging a slider is one undo step, not one per frame.
- Rename an object in the Name field. The rename applies when you press Enter or click away, as one undo step,
  and selecting another object first applies a name you were still typing.
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
- A Light Renderer's Type is Point, Spot or Directional. A directional light is a sun: it shines along its Direction from everywhere, so its position does not matter. Point and spot lights have a Range (0 means unlimited); a ranged light fades out, so raise Diffuse above 1 to light anything far away. Only a directional light has a Shadow Distance.
- Lights show a small icon at their position while the scene is stopped. Ctrl+click it to select the light.

### The transform gizmo

The selected object shows a gizmo for moving, rotating or scaling it.

- Switch modes with the Move, Rotate and Scale buttons in the Scene Status panel, or with the keys 1, 2
  and 3 (the default keybinds, listed under Settings below).
- The Local checkbox in Scene Status (on by default) picks the object's own axes instead of world axes. It is
  disabled in Scale mode, which always uses the object's own axes.
- Snapping is set in Settings, under Viewport: a Snap checkbox (off by default) and step sizes for
  translate (0.5), rotate (15 degrees) and scale (0.1).
- Dragging a handle sends updates while you drag, so other connected views see it move. Releasing the
  handle records one undo step.
- The gizmo is hidden, and takes no clicks, while the View is set to a scene camera instead of the Editor
  Camera. Its mode and option controls stay usable for when you switch back.

### Terrain mode

Select one object that has a Terrain collider and check Sculpt in its Terrain section (or press T) to shape the
terrain with a brush. It works only while the editor can edit the server.

- Pick Add, Subtract, Smooth, Flatten or Paint in the Terrain section, or press Alt+1 to Alt+5. Radius, Strength and
  Falloff are remembered for each tool. Add, Flatten and Paint use the material chosen in the Material swatches: Add and
  Flatten give the ground they raise that material, and Paint recolors existing ground without changing its shape.
  PageUp and PageDown grow and shrink the current tool's radius.
- A ring follows the cursor on the terrain. Left-drag to sculpt; Flatten levels to the height where you press. A
  stroke follows the ground as it was when you pressed, so a drag draws along the terrain instead of piling toward
  the camera; drag up past the horizon to raise a column on an upright plane through the stroke's last point. Ground
  more than two brush radii from the stroke's last point (a wall behind a column) does not catch the line. Hold
  still to keep building: Add slowly grows toward the camera and Subtract digs deeper. Subtract always works on the
  terrain that is there now, so erasing reaches whatever is left, even nearer than where you pressed, and it finds
  terrain anywhere under its ring, not only under the cursor.
  Right-drag still flies the camera.
- Ctrl+click picking and the transform gizmo are off while Sculpt is on. Changing the selection, starting Play or
  losing the connection to the server turns Sculpt off.
- Looking through a scene camera instead of the Editor Camera hides the ring and blocks sculpting, but Sculpt
  stays on for when you switch back.
- Sculpting works only while the scene is stopped.
- A radius larger than 64 voxels is limited to that size.
- Undoing strokes is not available yet.

#### Terrain materials

The Terrain section lists the terrain's materials under Materials, a header you can click to collapse. A new terrain
starts with one, a gray named Ground.

- Each line shows a material's swatch (its color, or a thumbnail of its texture) and its name. The list scrolls once it
  holds more than eight.
- Click a line to select it. The selected material is the one Add, Flatten and Paint use, and its details show below
  the list: its name, Color or Texture, its textures, Move up, Move down and Remove.
- Add material appends an entry (up to 16) with its own color and selects it. Type in the name field to rename the
  selected material, and use Move up and Move down to reorder it. Painted ground keeps its look when materials move.
- Each material is either a Color or a Texture. Color shows a color swatch: click it to pick the color. Texture shows an
  Albedo slot: drag a texture from the Assets panel onto it. Switching to Color removes the albedo texture. Either way,
  the Specular slot takes a specular map.
- Remove deletes the selected material and selects its neighbor. If the material is painted on the terrain, a window
  asks what the painted ground becomes (the material before it, unless you pick another); Remove there repaints that
  ground and removes the material, and Cancel keeps it. Ground painted with later materials keeps its look. The last
  material cannot be removed.
- Materials can be edited only while the editor can edit the server and the scene is stopped. Undo and redo cover
  every change; removing or moving a material is one undo step that restores the palette and the painted ground
  together. Removing or moving a material waits until the whole terrain has arrived from the server and no brush
  stroke is still finishing.

## Scene Status

The "Scene Status" panel along the top holds the play controls and some view choices. On a narrow window the
controls wrap onto more rows and the panel grows to fit.

- The play, pause and stop icon buttons. Play also resumes a paused scene. Buttons that do not apply are
  grayed out.
- A readout of the scene name and its state: Stopped, Running or Paused.
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
  New Scene, New Prefab, New Script and New Terrain. The Assets menu also has Import Model and Import
  Texture, which open a file dialog. New Terrain asks for a name, a voxel size and either a flat footprint or
  a heightmap image (the footprint is then the image size times the voxel size) with a height scale, all in
  meters, and adds the terrain and an object that shows it to the active scene.
- Double-click a scene to make it the active scene. You can also drag a scene onto the viewport.
- Drag a model or texture onto a matching slot in the Inspector. Drag a prefab onto the Objects panel to
  instantiate it, or onto an object row to instantiate it as a child.
- Selecting an asset shows it in the Inspector. Models, textures, scripts and prefabs can be renamed
  there, which changes only the display name. A scene can be renamed too, which changes the scene's real name;
  a name another asset already uses is refused. Prefab contents are editable in place.
- The Inspector's Delete Asset button removes an asset after a confirmation. For a scene it also deletes the
  scene's objects and scene scripts; Edit > Undo restores the whole scene. You can't delete the active scene
  or a project's only scene, and scenes can only be deleted while the scene is stopped.
- An object whose model or texture is missing, for example after its asset was deleted, draws as a magenta and black
  checker. Undoing the delete or picking another asset brings the real one back.

## The Console panel

Open it from Window, then Console. It shows the editor's log and the server's log in one list. Lines from
the server are prefixed with "[server] ". You can:

- toggle each level (trace, debug, info, warn, error, each with a count) and each category;
- search the text, with the Filters button next to the search box hiding or showing those toggles;
- Copy the filtered lines to the clipboard, Clear the list, and turn Auto-scroll on or off; these sit on the
  search line when the panel is wide enough.

Output from a script's `Log.info` and similar calls appears here under the script category. Compile errors
in your scripts appear here too.

## Settings

Open Window, then Settings. It has four sections.

- Appearance: the editor's colors.
- Viewport: camera speed, Show Grid, the Stats readout (frame, server tick and network figures in the viewport
  corner) and the gizmo snap options. The grid shows only through the Editor Camera.
- Display: Vertical sync and a frame rate limit (10 to 1000 fps, or Unlimited frame rate). Type a limit and press
  Enter or click away to apply it. The section shows the present mode in use; if the display cannot turn vsync off,
  it says so. Below that, Rendering has a Ray tracing checkbox. It is a local setting the editor remembers, grayed
  out if the device cannot ray trace.
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
| Toggle Profiler | F3 |
| Terrain: Toggle Mode | T |
| Terrain: Add | Alt+1 |
| Terrain: Subtract | Alt+2 |
| Terrain: Smooth | Alt+3 |
| Terrain: Flatten | Alt+4 |
| Terrain: Paint | Alt+5 |
| Terrain: Grow Brush | PageUp |
| Terrain: Shrink Brush | PageDown |

Holding Ctrl+Z, Ctrl+Shift+Z or Ctrl+D repeats the action; holding Ctrl+D adds a copy on each repeat, and each
copy is its own undo step. Ctrl+A in the Objects panel is local to that panel and is not a rebindable action.
Focus Selection and the Terrain actions can be bound, but they have no behavior yet. Keybinds are ignored while a
text field wants keyboard input.

## Saving and opening

- File, then Save (Ctrl+S) writes the project to disk. Save As... (Ctrl+Shift+S) picks a new location.
- File, then New and File, then Open replace the whole project. Dropping a project file on the window opens
  it too.
- If you have unsaved edits, New, Open and closing the window ask whether to Save, Don't Save or Cancel.
