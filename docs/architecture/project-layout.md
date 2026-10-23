# Runtime directory and project layout

The root `AGENTS.md` summarizes this topic and links here. Read this file before changing the code it describes,
and update it in the same change when that behavior moves.

Asset paths (`assets/models/...`), managed assembly
paths (`net/Transport/...`, `scripts/...`), and `nethost.dll` are all resolved relative to it. The
server's `defaultAssets/` are copied into `bin/assets/` at configure time. A model or texture path in a
project resolves against the project root first (`resolveAssetPath`), then against the executable's
directory, so built-in assets need no copy; a relative path that climbs out with `..` is refused. The
project root is a client-side notion: each app takes it from its own `--project` (a project folder or its
`project.ecs3d`; a legacy `.json` file has none), and `GpuAssetCache` applies it. A remote editor or
client started without the folder has no root and falls back to the built-in assets. The editor's Save,
Open and asset import use that layout. Open and Save As update the root. A new root, or a reopened project,
schedules a reset of the loaded models, textures and render objects. `RenderSystem::variableUpdate` applies it
before the frame's submissions by handing each to the engine's deferred release, which destroys it only after
the last frame that could use it has finished. An imported model or texture is copied to
`<root>/assets/models` or `assets/textures`, and `importDestination` adds `-1`, `-2` on a name clash. It is
registered with the relative path. A new script goes to `<root>/scripts`. A project with no file (the built-in
default) or whose folder holds a `template.json` is a template (`ProjectOrigin.h`'s `isTemplateProject`). The
editor opens one as "Untitled (from" plus the template's name, and its first Save acts as Save As into a
new folder. Until then an import or a new script (`assetWritesBlocked`) logs why, starts Save As and
writes nothing, so the template stays as it was. The spawned server gets the
project by the `loadProject` blob. Its own root, used for scripts, stays what `--project` gave it.
Dialog and drop paths arrive as 8-bit Unicode text (`pathFromUtf8`), and logs use `pathToUtf8`.
The server compiles `<root>/scripts/**/*.cs` together with the built-in scripts (see [scripting.md](scripting.md)).
