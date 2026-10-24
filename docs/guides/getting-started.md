# Getting Started

This guide takes you from a built engine to a playable project: what the build gives you, how to open a
template, and how to walk around in it. It assumes ECS3D is built.

## What is in `bin/`

The build writes everything to a `bin/` folder inside the CMake build folder, for example
`cmake-build-ecs3d-debug/bin` with the Debug preset:

- `ECS3DLauncher`: the launcher, a small window that finds the engine and starts the editor.
- `ECS3DEditor`: the editor, where you build and try scenes.
- `ECS3DClient`: the game client, which plays a project without the editing tools.
- `ECS3DServer`: the server, which runs the simulation. The editor and client start one for you.
- `templates/`: the shipped templates and samples, one folder each (First-Person, Coin Rush and others).

On Windows each program has an `.exe` extension. The programs read their assets, scripts and templates relative
to the folder they are run from, so run them from `bin/`. The launcher does this for you. The commands in these guides use bare program names.
On Linux and macOS prefix a program with `./`, and in PowerShell with `.\`, for example
`./ECS3DEditor --project templates/first-person`.

## The launcher

The launcher is the front door. It looks for the three engine programs in its own folder. If one is missing, a
banner at the top names the missing file and the folder it looked in, and its Open Settings button goes to the
Settings page. There you can point the launcher at a different engine folder, change the default location for new
projects and choose whether the editor and its server open a console window. The launcher saves automatically, and
a text field saves when you leave it.

The sidebar lists Projects, Templates, Samples and Learn. On first run the Projects page is empty ("No recent
projects yet"). Its Explore the engine card opens the editor on the built-in Welcome scene. That project is
unsaved, so nothing is written until you choose to save it.

## Start a project from the First-Person template

The First-Person template is a lit ground with crates, ramps and four spawn points.

1. Open the Templates page and click the First-Person card. The header's New Project button and the Projects
   page's New Project card open the same dialog, with the Empty 3D template chosen.
2. In the New project dialog, type a Project name. The Location starts in `ECS3D Projects` under your Documents
   folder; use Browse... to pick another. "Will be created at" shows the folder the dialog will make, and the
   dialog tells you when that folder already exists or the name cannot be used.
3. Leave "Open in the editor when it is created" checked, then click Create project.

The launcher copies the template into your new folder and opens it in the editor. From then on you are working in
your own project: Save (Ctrl+S) writes into that folder, and the template itself is never changed. The project
appears on the Projects page, where its card opens it in the editor again, and its menu plays or hosts it (see
[Multiplayer](multiplayer.md)).

Without the launcher, open the template straight from `bin/`:

```
ECS3DEditor --project templates/first-person
```

A project folder that holds a `template.json` is a template, and the editor never writes into it. It opens as
"Untitled (from First-Person)", and that name is in the window title. The first Save (Ctrl+S) asks you for a
new folder and starts in `ECS3D Projects` under your Documents folder.

## The editor

The editor has an Objects panel for the scene tree, an Inspector for the selected object, an Assets panel, a
Console and the viewport. Every change you make is sent to the editor's own server. See
[Editor basics](editor-basics.md) for each panel, the gizmo, undo and saving.

## Play and stop

A scene always starts Stopped. The Scene Status panel along the top has the play, pause and stop buttons. Press
Play and the scene runs: scripts and physics start.

**Stop discards every runtime change.** Anything a script spawned, anything destroyed and any edit you made
while playing is thrown away, and the scene returns to what you authored.

## Controls

| Action | Default |
|--------|---------|
| Move | W, A, S, D (the arrow keys also move) |
| Jump | Space |
| Look | Hold the right mouse button and drag |

These are fields on the Player prefab's `PlayerScript`, so you can change the keys in the Inspector.

In the editor, the mouse reaches the game only while the viewport looks through a scene camera. After pressing
Play, choose your player's camera in the View combo in Scene Status. Choosing a slot in the Player combo
points the View at that player's camera too. With the Editor Camera, the right-drag turns the editor's own
camera instead.

To play without the editing tools, choose Play from the project card's menu (the ... button) in the launcher.
It runs a client on the project folder, and the client starts its own local server. The command-line
equivalent, from `bin/`, is:

```
ECS3DClient --project templates/first-person
```

## Next steps

- [Editor basics](editor-basics.md): every panel, the gizmo, undo and saving.
- [Your first script](your-first-script.md): write a C# script and attach it to an object.
- [Scene scripts and events](scene-scripts-and-events.md): scripts for the whole scene, and events between
  scripts.
- [Starter scripts](starter-scripts.md): ready-made scripts you can drop on an object.
- [Multiplayer](multiplayer.md): host a game and let friends join over your network.
