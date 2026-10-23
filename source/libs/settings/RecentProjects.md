# recent-projects.json

The list of recently opened projects. The editor (through `RecentProjects`) and the C# launcher both read
and write it, so this page is the one description of the schema. It lives in the per-user directory that
`userDataDirectory()` resolves: `%APPDATA%/ECS3D` on Windows, `~/Library/Application Support/ECS3D` on macOS,
`$XDG_CONFIG_HOME/ECS3D` or `~/.config/ECS3D` on Linux.

```json
{
  "version": 1,
  "projects": [
    {
      "path": "C:/Users/Alex/Documents/ECS3D Projects/Courtyard",
      "name": "Courtyard",
      "lastOpenedUtc": "2026-10-18T19:02:11Z",
      "pinned": true,
      "template": "first-person"
    }
  ]
}
```

## Fields

| Field | Rule |
| --- | --- |
| `version` | Currently 1. A writer only raises it, so a file from a newer build keeps its own. |
| `projects` | Newest opened first. Readers that want pinned entries first sort for themselves. |
| `path` | Required string, UTF-8. Absolute, forward slashes, no trailing slash (a root keeps its own). A project folder, or a legacy `.json` file; a path to `project.ecs3d` or any other `.ecs3d` file is recorded as its folder. Unique: compared case-insensitively (ASCII) on Windows and macOS, case-sensitively elsewhere. |
| `name` | Folder name, or the file name without extension for a path ending in `.json`. Recomputed on every open. |
| `lastOpenedUtc` | ISO 8601 UTC with a `Z`, whole seconds, such as `2026-10-18T19:02:11Z`. Read strictly: exact digits and a real calendar date, else the time counts as unparseable. |
| `pinned` | Optional boolean, default false. |
| `template` | Optional string: the template or sample id the project was created from. Preserved by every writer, written only by the launcher. |

## Writer rules

- Keep fields you do not know, both top-level and per entry.
- Write atomically: a uniquely named temporary file (`recent-projects.json.<hex>.tmp`) in the same
  directory, then a rename over the target. Retry the rename a few times, since the other program may hold
  the target open. A failed write is logged and leaves no temporary file behind.
- Compare paths after normalizing both sides (separators, trailing slash, case on Windows and macOS), so an
  entry written as `C:\x\y` matches `C:/x/y`. Several entries for one path collapse into the first.
- Keep at most 50 entries. Over the limit, drop the oldest unpinned entry (by `lastOpenedUtc`; a missing or
  unparseable time counts as oldest, and on a tie the later entry goes) until 50 remain. Pinned entries and
  the entry just recorded are never dropped, so a list of 50 pinned entries grows to 51.
- Treat an unreadable file (bad JSON, or `projects` not an array) as empty, log it once, and replace it on
  the next write. A file that cannot be read at all (a sharing violation, a lock) is not replaced: the launcher
  skips its change and tries again later. A missing file is simply empty. A file with no `projects` key is an
  empty list whose other top-level fields are kept. An entry that is not an object, or has no string `path`, is dropped and
  logged.
- Opening a project that is already listed moves its entry to the front and keeps its other fields.

Two programs writing at once is not coordinated: the last write wins.
