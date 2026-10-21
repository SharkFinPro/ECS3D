# launcher-settings.json

The launcher's own preferences, in the per-user data folder (`<user data>/launcher-settings.json`). The editor
does not read it. `LauncherSettingsStore` owns it.

| Field | Type | Default |
| --- | --- | --- |
| `version` | number | 1 |
| `engineDirectory` | string or null | null (the launcher's own folder; empty means the same) |
| `projectsDirectory` | string | `<Documents>/ECS3D Projects` (`<home>/Documents` on Linux and macOS) |
| `editorConsole` | bool | false |
| `serverConsole` | bool | false |
| `closeAfterOpen` | bool | false |
| `hostPort` | number, 1-65535 | 3000 (`EnginePorts.Default`, mirrors `defaultPort` in `Protocol.h`) |
| `lastJoin` | `{ address, port }` or absent | absent |
| `projectsView` | `"grid"` or `"list"` | `"grid"` |

- A missing file means all defaults. An unreadable file, or one whose top level is not an object, gives defaults,
  one warning in the launcher log, and is replaced by the next save.
- A field of the wrong type or out of range takes its default and logs a warning; the other fields keep their values.
- Unknown fields, including inside `lastJoin`, survive a load and save.
- Writes go to a uniquely named `<file>.<id>.tmp` and then replace the target, so a failed write leaves the old file.
