# AGENTS.md - ECS3DProfile

Agents load this file when they read a file in `source/libs/profile/`. It holds what a change here must keep. Read
[docs/architecture/profiling.md](../../../docs/architecture/profiling.md) before changing the lane or the ticket scheme:
the suite pins their edge cases.

- The library depends on the standard library only. Do not link `ECS3DLog`, json or glm into it.
- `Lane::enter` and `Lane::exit` must not allocate, except when interning a new node, and must not throw. Keep the
  stack and the per-sample accumulators fixed-size arrays.
- One lane belongs to one thread. Read and write it only on that thread; nothing in it is synchronized.
- Wrap phases in a scope, never per-object work inside a loop, so a scope's cost stays small next to what it times.
- Never place a scope inside an OpenMP parallel region: the worker threads have no bound lane, so it would silently
  record nothing.
- A change to the ticket encoding must keep a stale `exit` harmless: the sample sequence and the depth both have to
  match.
- Tests drive a `Lane` with an injected clock and never assert on the real one. Unbind the lane at the end of each test.
