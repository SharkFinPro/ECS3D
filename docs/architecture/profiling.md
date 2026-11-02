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

## Switches

- `ECS3D_PROFILER_SCOPES` is a CMake option (default ON) that defines the macro of the same name for every consumer.
  With it off, `ECS3D_PROFILE_SCOPE` expands to nothing. It is separate from `ECS3D_PROFILE`.
- `profile::setEnabled(false)` is a global runtime switch (default on) that makes every `Scope` a no-op.
