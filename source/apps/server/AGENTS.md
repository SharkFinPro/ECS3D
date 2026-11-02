# AGENTS.md - ECS3DServer

Agents load this file when they read a file in `source/apps/server/`. It holds what a change here must keep. The
full notes are in [docs/architecture/apps.md](../../../docs/architecture/apps.md) and
[docs/architecture/replication.md](../../../docs/architecture/replication.md).

- The server is headless and the only app that links `ECS3DSim` and `ECS3DScripting`. It does not link render or
  editor.
- `ServerApp` accepts a mutation message only on an `--edit` server and only from a connection the transport
  authorized as `Role::editor`, through `isMutationAuthorized` in `ServerPolicy.h`. The test suite includes that
  header, so policy decisions belong there. It also includes `StructuralChanges.h`, which holds the order
  `broadcastStructuralChanges` replicates a tick's spawns, destroys and component changes in.
- The server lane's sample-close rule lives in `ServerPolicy.h` and the stats line text in `StatsSummary.h`; the suite
  includes both. See [profiling.md](../../../docs/architecture/profiling.md).
- A loaded scene starts stopped until the editor presses play; a play server starts its scene itself.
- Structural changes from a tick replicate after it: `broadcastStructuralChanges` flushes queued script spawns and
  deletions, then sends the incremental messages.
- The log reaches editor connections through a `RemoteLogSink` only on an `--edit` server, since a play server admits
  an editor without a token. `forwardDrainedLog` in `ServerPolicy.h` sets each batch's caps.
- Which refused edits answer with a resync snapshot is decided in `ServerPolicy.h`.
- `DefaultProject.cpp`, `DefaultScenes.cpp`, `DefaultShapes.cpp`, `DefaultWelcome.cpp`, `RestPose.cpp` and
  `ShutdownSignal.cpp` compile into the test suite. The shape formulas mirror `tools/shapes/library.py`, and a test pins coordinates from both.
- Each file in `defaultAssets/` has its source and license in `defaultAssets/LICENSES.md`.
