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
  records its wall time, the micros and call count of every node, and up to eight named counters, then goes into the
  lane's `History`, a fixed ring that overwrites the oldest sample. Nodes are saturating `uint32_t` micros and
  `uint16_t` call counts.
- A **lane** is one thread's record: a tree, a history, counters and an optional tracked scope. `trackScope` also
  pushes the duration of every completed scope with that name, at any depth, into a `DurationRing`.

## Binding

`Lane::bindToThisThread()` points a thread-local at the lane. A `Scope` records into the lane bound to the thread it
runs on and does nothing on a thread with no lane, while profiling is disabled, or when the lane has no open sample.
Binding again replaces the lane, and destroying a lane unbinds it. A lane is read and written only on its own thread,
and it must outlive the scopes that record into it.

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

## Switches

- `ECS3D_PROFILER_SCOPES` is a CMake option (default ON) that defines the macro of the same name for every consumer.
  With it off, `ECS3D_PROFILE_SCOPE` expands to nothing. It is separate from `ECS3D_PROFILE`.
- `profile::setEnabled(false)` is a global runtime switch (default on) that makes every `Scope` a no-op.

## Statistics

`ProfileStats.h` works over the newest `window` samples of a `History`, or all of them when there are fewer.

- `percentile` uses the nearest-rank method: rank is `ceil(p / 100 * n)`, one-based, clamped to `[1, n]`; no values
  give 0.
- `Summary` holds the last value, the average, the 95th percentile, the maximum and the sample count, for wall time
  (`summarizeWall`) or one node's micros (`summarizeNode`).
- A hitch is a sample whose value is more than twice the median (`isHitch`); a median of zero never counts.
  `countHitches` compares wall time against the window's median.
