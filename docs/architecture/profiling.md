# Profiling

The root `AGENTS.md` summarizes this topic and links here. Read this file before changing the code it describes,
and update it in the same change when that behavior moves.

`ECS3DProfile` (`source/libs/profile/`, namespace `profile`) times named scopes into a per-thread record and keeps a
fixed history of the results. It uses only the standard library and links no other ECS3D library.

## Model

- A **scope** is a named span of code: `ECS3D_PROFILE_SCOPE("name")` declares a `profile::Scope` that measures from
  its construction to the end of the enclosing block. The name is interned once per call site into a `NameId`.
- A **node** is a scope's place in the nesting. `ScopeTree` interns nodes at run time: node 0 is the root, and the same
  name under two parents is two nodes.
- A **sample** is one unit of work, usually one frame or tick, between `Lane::beginSample()` and `endSample(true)`. It
  records its wall time and the micros and call count of every node, then goes into the lane's `History`, a fixed
  ring that overwrites the oldest sample. Micros are saturating `uint32_t` and call counts saturating `uint16_t`.
- A **lane** is one thread's record: a tree and a history.

## Binding

`Lane::bindToThisThread()` points a thread-local at the lane. A `Scope` records into the lane bound to the thread it
runs on and does nothing on a thread with no lane, while profiling is disabled, or when the lane has no open sample.
Binding again replaces the lane, and destroying a lane unbinds it. A lane is read and written only on its own
thread, and it must outlive the scopes that record into it. Unbind or destroy a lane on the thread it is bound to;
destroying it on another thread leaves that thread's pointer dangling.

The macro expands to two statements, so use `ECS3D_PROFILE_SCOPE` once per line, as a statement in a braced block, and
never as the body of an unbraced `if` or `for`.

## Caps

The depth cap is eight (`maxDepth`) and the node cap is 128 (`maxNodes`). A scope past either cap records nothing, and
its time stays inside its parent's. `overflowCount()` counts it. Scopes nested under a folded scope fold too, so they
never attach to the wrong parent.

## Tickets and abandoned scopes

`Lane::enter` returns a ticket that combines the sample's sequence number with the stack depth, and `exit` ignores a
ticket from another sample or another depth. Closing a sample with scopes still open drops them and adds them to
`abandonedScopeCount()`; their later destruction is then harmless, even if a new sample has started. Folded scopes get
a ticket with depth zero, so they also stay tied to their own sample. The enter and exit path does not allocate unless
it interns a new node, and it never throws: an allocation failure folds the scope.

## Counters and tracked scopes

- A **counter** is a per-sample integer. `Lane::counterId(name)` interns it per lane (at most `maxCounters` = 8, then
  `std::length_error`), and `addCounter(id, value)` sums into the open sample; it is ignored with no open sample or an
  unknown id. Counters reset in `beginSample` and are stored in `Sample::counters`.
- `Lane::trackScope(name, capacity)` makes every completed recorded scope with that name, at any depth, also push its
  micros into a `DurationRing`, a fixed ring read through `Lane::tracked()` (null until tracking starts).
  `totalPushed()` counts every push since construction, even after the ring wraps.
- The ring is allocated by `trackScope`, so `exit` stays allocation-free, and the check is a `NameId` compare. Folded
  scopes record nothing, so they push nothing. Calling `trackScope` again replaces the ring and its name, so a pointer
  from an earlier `tracked()` call is no longer valid.

## Statistics

`ProfileStats.h` summarizes a `History` over a **window**: the newest `window` samples, or all of them when there are
fewer. `Summary` holds `last`, `average`, `p95`, `max` and `samples`; an empty history gives zeros, and
`summarizeNode` gives zeros for a node id at or past `maxNodes`.

- `percentile(values, p)` is nearest rank: the sorted value at 1-based rank `ceil(p / 100 * n)`, clamped to `[1, n]`,
  and 0 for no values; a NaN `p` is treated as 0. The 95th percentile of 1..20 is 19.
- A **hitch** is a wall time more than twice the window's nearest-rank 50th percentile (the lower middle value for an
  even window); a zero there never counts. `countHitches` counts them over the window.

## Server lane

`ServerApp` owns a lane named `server` (512 samples), bound to the thread that runs `run()` and unbound when `run()`
ends. It tracks `Server tick` with a 512-entry ring and interns the counters `ticks`, `droppedSteps` and `objects`.

- **Sample rule:** each `run()` iteration begins a sample (a no-op while one is open). `shouldCloseServerSample` in
  `ServerPolicy.h` closes it when the iteration ran a step or the sample is a fixed step (20 ms) old, so idle spins
  fold into one sample. The 1 ms sleep is outside every scope.
- **Counters:** `ticks` adds the iteration's steps only while the current scene is running (a stopped or paused scene
  plans steps that do nothing and opens no `Server tick` scope). `droppedSteps` adds the whole steps `FixedTimestep`
  discarded past its cap (`Plan::droppedSeconds`). `objects` adds the current scene's object count once, as the sample
  closes. `ServerApp` also keeps running totals of ticks and dropped steps, and the summary reports their change since
  the previous summary, so a long interval is not capped by the history.
- **Scopes:** `Network in`, `Server tick` (each step, including the input-state calls) holding `Scripts`
  (`variableUpdate`, `fixedUpdate`), `Physics`, `Collisions` and `Script events`; `Replication` (`Structural`,
  `State delta`) at the top level; `Snapshot`, `Log forwarding`, `Script reload check` and `Stats`.
- **Summary line:** every `--stats-interval` seconds `logStatsIfDue` logs one line built by `StatsSummary.h`. The tick
  figures (average, p50, p95, p99 and max, in milliseconds rounded from microseconds) come from the `Server tick`
  durations pushed since the previous line. The per-sample averages of `Scripts`, `Physics`, `Collisions` and
  `Replication` come from the samples closed since then. Ticks and dropped steps are the change in the running totals,
  and the object count is the newest sample's. The line ends with the traffic since the previous line: `sent` (bytes
  handed to the transport, with the two largest message types), `recv`, `conns` (the connection count now) and
  `snapshots` (how many were sent, with the largest one's size and reason). `refused N` follows `recv` when sends were
  refused as too large. Sizes are `N B`, or `N.N KB` / `N.N MB` in
  1024s.
- **Covered seconds:** the wall time the summarized samples span. It can be less than the interval when the history is
  shorter.
- **Idle:** when no running-scene tick happened in the interval, it logs `stats: idle (no ticks)` once per idle streak.
  An idle interval that still sent or received bytes or recorded a snapshot (a stopped edit server taking scene edits)
  is logged every time, as `stats: idle (no ticks) | sent ... recv ... | conns N | snapshots ...`.

## Server statistics on the wire

`MessageType::serverStats` carries a server lane's numbers to an editor, and `MessageType::statsSubscribe` (a bool)
asks for them. `net/ServerStats.cpp` owns the layout and its caps (`packServerStats`, `unpackServerStats`), version 1:
the flags (debug build, edit mode, scene running, node table present), the node-table epoch, then optionally the node
table (each node's parent and name; node 0 is the root and has no entry), the samples with their non-zero nodes, the
`Server tick` durations, per-type traffic in each direction, snapshot records with their age in milliseconds, and the
connection, editor and object counts. Unpack refuses any other version, a truncated payload, a node parent that is not
an earlier node, an unknown snapshot reason and every count past its cap or past what the remaining bytes could hold,
before it reserves memory.

## Server statistics to editors

`ServerApp` honors `statsSubscribe` from any connection the transport authorized as `Role::editor`, on an edit or a
play server (`mayReceiveServerStats` in `ServerPolicy.h`); a player's attempt is refused with one warning per
connection. A subscription lasts until the connection unsubscribes or drops, so an editor subscribes again after every
join. `net::StatsSubscriptions` keeps the subscribers and the node-table epoch each was last sent.

`ServerApp::sendServerStatsIfDue` runs after `logStatsIfDue` and sends at most every `serverStatsPeriod` (250 ms).
`collectServerStats` (`ServerStatsFeed.h`) gathers everything since the previous message: the closed samples, the
`Server tick` durations, per-type traffic differences and the snapshot records with their age. With no subscriber
`skipServerStats` only advances the cursor, so a first message carries no backlog. The epoch is the scope tree's node
count, which only grows; the table goes only to a subscriber last sent a different epoch, and a new subscription
starts with none, so its first message carries it. Messages go out through `NetServer::sendToConnections`.

`net::RemoteStats` (`net/RemoteStats.h`) is the editor's copy of that data, touched on the main thread only. It rebuilds
a `profile::History`, the tick ring, the node table (kept as strings and never interned, so a remote peer cannot grow
the editor's name table), the traffic batches and the snapshot records from each `serverStats` message. It is `idle`
until `subscribed()` clears it and starts `waiting`; a first message makes it `live`, and two seconds of silence, or a
version other than 1, make it `unsupported` (a later good message turns it live again). `disconnected()` keeps the data
for display. Sample entries naming a node the held table lacks are skipped and counted.

Tick durations are taken as their scopes exit, so a message can carry durations from a sample that is still open, and
each duration is sent once, in a later message than its sample. A failure to build or send is logged once per run of
consecutive failures and the server keeps running; both packed copies are built before the node-table epochs are
recorded, so a failure loses no subscription state.

## Display model

`ProfilerView.h` is the headless model the Profiler panel draws from; it is pure computation over a `History` and a
`ViewNode` table (`viewNodes(ScopeTree)` for a local lane, or filled from a remote node table).

- **Groups.** Each `LaneKind` has a fixed, ordered list of `DisplayGroup`s ending in "Other": frame lanes show Network
  in, Input, Editor UI, Scene prep, Renderer, Profiler; the server lane shows Scripts, Physics, Collisions,
  Replication, Network in. One rule table per kind in `ProfilerView.cpp` maps scope names (and, for the server's
  phases, the parent `Server tick`) to groups; a node takes its nearest matching ancestor-or-self, else Other. A
  top-level rule matches only a node whose parent is the root, `Snapshot` maps to Replication at any depth (joins
  record it under `Network in`, reloads under `Script reload check`), and a cyclic or out-of-range parent gives Other.
- **Exclusive time.** `groupMicros` charges each node its micros minus its direct children's (clamped at 0), so
  nested scopes are not counted twice. Hidden groups read 0.
- **Window.** `windowSize(history, seconds)` counts the newest samples whose end falls within the last `seconds`.
  Negative or NaN seconds count as 0.
- **Auto scale.** `autoScaleMillis(p99)` picks 16.7, 33.3, 50 or 100 ms, then whole multiples of 100 ms.
- **Columns.** `buildColumns` gives one column per sample; past `maxColumns`, adjacent samples merge by max, with
  hitch OR'ed, so a spike survives downsampling. A column's `sequence` is its newest sample, which a click selects.
- **Breakdown.** `breakdownRows` lists active nodes depth first with the selected (or newest) sample's value plus the
  window's average, p95, max and share of the average wall time. An inactive node stays, with zero statistics, when a
  descendant is active. Nodes whose parent chain does not reach the root are left out.

## Switches

- `ECS3D_PROFILER_SCOPES` is a CMake option (default ON) that defines the macro of the same name for every consumer.
  With it off, `ECS3D_PROFILE_SCOPE` expands to nothing. It is separate from `ECS3D_PROFILE`.
- `profile::setEnabled(false)` is a global runtime switch (default on) that makes every `Scope` a no-op.

## Client and editor lanes

`ClientApp` and `EditorApp` each own a `Lane` (`"client"`, `"editor"`, 4096 samples) that `run()` binds to the main
thread and unbinds on every exit path. One sample spans one `run()` iteration: the loop closes the previous sample and
opens the next at its top, so the previous iteration's `render()` falls inside its own sample. Top-level scopes are
`Network in` (with `Snapshot apply` nested under it), `Input`, `Scene prep` and `Renderer`; the editor adds `Editor UI`.
The editor's lane is drawn by the Profiler panel through `ProfilerCharts` (`ECS3DRender`), which holds the ImGui
drawing for the bars, legend and breakdown table; its pixel math is the headless `ProfilerChartLayout.h`. Nothing
else reads the editor's lane yet. The app wiring has no headless test.

The client draws its own lane as a top-right overlay (`ClientApp::displayStatsOverlay`, scoped as `Profiler`) that F3
cycles through off, compact and detailed (`StatsOverlay.h`; `--stats` starts compact). Compact shows the frame time
and fps over one second plus the inbound rate; detailed adds the frame chart over five seconds, the state-update
interval, the top five inbound types and the last snapshot's size. The interval is the `stateInterval` counter on the
client lane, in microseconds: `run()` times each drained `stateDelta` against the previous one
(`StateUpdateGap`) and adds the frame's largest gap, so frames without a stateDelta hold 0 and are skipped by
`summarizeIntervals`. Inbound rates come from `net::TrafficRateMeter` (`TrafficRates.h`) over `NetClient::traffic()`,
recomputed every 500 ms and updated every frame, whatever the overlay mode, so it never averages over the time the
overlay was off. The interval is measured at drain, so a client frame stall or a server pause also shows as a long
interval.

## Server lane in the Profiler panel

The panel draws the server's tick lane below the editor lane, from `net::RemoteStats`. `EditorApp::updateServerStats`
(`EditorAppProfiler.cpp`) sends `statsSubscribe(true)` when the panel opens or the viewport corner readout
(`editor.viewport.statsReadout`, `ProfilerReadout.h`) turns on and `false` when neither holds, so the server streams
nothing otherwise; it also calls `subscribed()`/`unsubscribed()` on the `RemoteStats` and
`disconnected()` when the connection is lost. Each frame the stats are wanted, it fills a `ServerLaneInput`
(`ProfilerServerLane.h`): the state (`off`, `waiting`, `live`, `unsupported`, `disconnected`), the history pointer, the
node table as `ViewNode`s (rebuilt only when the epoch or node count changes), the server flags and the figures below.
`serverLaneNotice` gives the line shown under or instead of the chart: waiting, unsupported, "Disconnected", or "Scene
stopped" while live with no scene running. The editor lane draws in every state. `updateServerStats` runs every frame
from the run loop, so the subscription and figures stay current while F10 hides the GUI. The readout is drawn inside the
`Renderer` scope, so its own `Profiler` scope groups under Renderer, not Profiler.

The Network section shows four traffic lines (`describeAllTraffic`), each with bytes per second, messages per second
and the largest message types: the server's sent and received (`net::summarizeTraffic` in `RemoteStatsSummary.h`,
over the panel's window, dividing by the server time the batches cover) and the editor's own, from two
`net::TrafficRateMeter`s over `NetClient::traffic()` (the lines use the meters' 500 ms intervals, not the window,
and the meters restart when the subscription flips). It also shows the last ten snapshots with their reason, size,
build time and recipients, the largest held snapshot, snapshots in the last minute (`net::snapshotsWithin`), and the
connection and editor counts; those server lines appear only once the server has reported (`hasServerFigures`), so
an older server shows just the editor's two lines. Counted bytes exclude the transport's framing, which adds 4 bytes
per message on TCP.
Byte rates divide by the server time the samples cover, which excludes the roughly 1 ms sleep between samples, so they
read slightly high.

## Captures

"Save capture..." in the panel header writes the window the panel shows (the frozen histories while paused) through
`writeCapture` (`ProfilerCapture.h`, compiled into the test suite; `ECS3DProfile` stays free of json and file I/O) into
`userDataDirectory() / "captures"` as `<app>-<yyyyMMdd-HHmmss>.csv` plus a `.json` sidecar. The CSV has one row per
sample, the editor lane before the server lane, with `frame.*` and `server.*` group columns holding `groupMicros`
(nothing hidden; the other kind's columns empty) and the server's `ticks` and `droppedSteps` counters; it is integers
only with LF endings, so no locale can change it. The sidecar holds the version, build type, platform, scene name,
object and connection counts, capture time, each lane's scope table and the snapshot rows; the scene name and object
count are the only project content. `EditorApp` supplies the environment each frame through
`ProfilerPanel::setCaptureEnvironment`; the version is the `ECS3D_ENGINE_VERSION` define on the editor target. The
guide is [profiling-your-game.md](../guides/profiling-your-game.md).

## Collision phases

`CollisionSystem` records `Broad phase` (building the edge list, bounding boxes, sort and dynamic flags), `Narrow
phase` (the whole OpenMP loop, timed once from the calling thread) and `Response` (pair assembly through
`recordCollisionEvents`). A scope never opens inside the parallel region, and with no lane bound they do nothing.
