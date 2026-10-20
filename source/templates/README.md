# Templates and samples

Each folder here is one template or sample, shown on a launcher card and used to create a new project.
The whole directory is copied to `<runtime output dir>/templates/` at configure time, this file included.
`TemplateValidationTest` validates every folder here, so a broken template fails the test suite.
The copy also holds this `README.md`, so code listing `templates/` must skip non-directories.

## Folder layout

```
<id>/
  project.ecs3d        the project JSON
  assets/              models and textures this template adds
  scripts/             .cs files this template adds
  README.md            optional; samples have one
  template.json        the manifest (below)
  thumbnail.png        640x360
```

Opening a folder that holds a `template.json` in the editor gives an untitled project. Its first Save asks for a
fresh folder, and imports and new scripts wait until then, so the template folder is left as it was.

Creating a project copies everything except `template.json`. Asset paths resolve against the project
folder first, then the engine's built-in `assets/`, so a template references built-in assets without
copying them.

## template.json

```json
{
  "id": "coin-rush",
  "name": "Coin Rush",
  "description": "A 1-4 player arena: grab coins, shove rivals into the void, win the round.",
  "category": "sample",
  "tags": ["multiplayer", "scripting", "game"],
  "thumbnail": "thumbnail.png",
  "entryScene": "Arena",
  "minEngineVersion": "0.0.1",
  "players": { "min": 1, "max": 4 }
}
```

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `id` | string | yes | Unique key, matching `[a-z0-9]+(-[a-z0-9]+)*`. |
| `name` | string | yes | Display name. |
| `description` | string | yes | One or two sentences for the card. |
| `category` | string | yes | `"template"` (a starting point) or `"sample"` (a finished example). |
| `tags` | array of strings | yes | Filter tags. May be empty. |
| `thumbnail` | string | yes | Image path relative to the folder. |
| `entryScene` | string | yes | Name of the scene in `project.ecs3d` that opens first. |
| `minEngineVersion` | string | yes | Oldest engine version that can open the project. |
| `players` | object | no | `{ "min": int, "max": int }` with `1 <= min <= max`. Absent means single-player. |

Unknown fields are ignored. The parser is `parseTemplateManifest` in `source/libs/data/templates/`.

## Uuid rules on create

Object uuids are regenerated, so two projects made from one template never share them. Asset, prefab and
scene uuids are kept, because scripts and spawners hardcode prefab uuids. Because object uuids change, no
shipped script field may hold an object uuid: reference objects by name instead.

## Scripts

A template's `scripts/` holds only classes unique to it. Project scripts and built-in scripts compile
together, so a class name that duplicates a built-in script fails the compile. Do not copy `PlayerScript`
or other built-in scripts into a template; to customize one, copy it under a new class name.

## Ground tiles

The `Ground` object stays at unit scale at y = -1. Its Box collider's own scale carries the floor's
half-extents (W/2, 1, D/2), so its top is at y = 0. Its `ModelRenderer.shouldRender` is off.

The visible floor is a set of tile children of `Ground`, each at local y = 1 with scale (10, 1, 10), using
`plane_tile.glb` and `grid_checker.png` (default assets). Those two assets arrive with the default assets change, so they may not exist yet. Each tile is 20 x 20 units with 1-unit cells.
