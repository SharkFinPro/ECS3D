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

## Switches

- `ECS3D_PROFILER_SCOPES` is a CMake option (default ON) that defines the macro of the same name for every consumer.
  With it off, `ECS3D_PROFILE_SCOPE` expands to nothing. It is separate from `ECS3D_PROFILE`.
- `profile::setEnabled(false)` is a global runtime switch (default on) that makes every `Scope` a no-op.

## Client and editor lanes

`ClientApp` and `EditorApp` each own a `Lane` (`"client"`, `"editor"`, 4096 samples) that `run()` binds to the main
thread and unbinds on every exit path. One sample spans one `run()` iteration: the loop closes the previous sample and
opens the next at its top, so the previous iteration's `render()` falls inside its own sample. Top-level scopes are
`Network in` (with `Snapshot apply` nested under it), `Input`, `Scene prep` and `Renderer`; the editor adds `Editor UI`.
Nothing reads these lanes yet. The app wiring has no headless test.

## Collision phases

`CollisionSystem` records `Broad phase` (building the edge list, bounding boxes, sort and dynamic flags), `Narrow
phase` (the whole OpenMP loop, timed once from the calling thread) and `Response` (pair assembly through
`recordCollisionEvents`). A scope never opens inside the parallel region, and with no lane bound they do nothing.
