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
| `path` | Required string. Absolute, forward slashes, no trailing slash. A project folder, or a legacy `.json` file. Unique: compared case-insensitively (ASCII) on Windows and macOS, case-sensitively elsewhere. |
| `name` | Folder name, or the file name without extension for a path ending in `.json` or `.ecs3d`. Recomputed on every open. |
| `lastOpenedUtc` | ISO 8601 UTC with a `Z`, whole seconds, such as `2026-10-18T19:02:11Z`. |
| `pinned` | Optional boolean, default false. |
| `template` | Optional string: the template or sample id the project was created from. Preserved by every writer, written only by the launcher. |

## Writer rules

- Keep fields you do not know, both top-level and per entry.
- Write atomically: a temporary file in the same directory, then a rename over the target. A failed
  rename is logged and leaves no temporary file behind.
- Keep at most 50 entries. Over the limit, drop the oldest unpinned entry (by `lastOpenedUtc`; a missing or
  unparseable time counts as oldest) until 50 remain. Pinned entries are never dropped, so a list of more
  than 50 pinned entries is kept whole.
- Treat an unreadable file (bad JSON, or `projects` not an array) as empty, log it once, and replace it on
  the next write. A missing file is simply empty. An entry that is not an object, or has no string `path`,
  is dropped and logged.
- Opening a project that is already listed moves its entry to the front and keeps its other fields.

Two programs writing at once is not coordinated: the last write wins.
