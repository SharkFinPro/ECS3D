# Dynamic AABB tree broad phase (prototype)

## What it does

`CollisionSystem` used to find candidate pairs with a sweep-and-prune over every collider, rebuilt each tick.
This prototype adds a second path that keeps two Box2D-style dynamic AABB trees (one for static bodies, one
for bodies with a RigidBody) and a persistent, sorted cache of proxy pairs whose fat AABBs overlap
(`source/libs/sim/broadphase/`).

Each tick:

1. `CollisionSystem` still builds and sorts the edge list and warms every bounding box. The sort is kept
   because the serial response pass breaks ties in edge-index order.
2. It hands one `BroadPhaseInput` per edge (tight AABB, the body's velocity as displacement, a dynamic flag)
   to `BroadPhase::update`.
3. `update` matches inputs to proxies by collider address, moves proxies in their tree (the leaf is only
   reinserted when the tight box leaves its fat box), destroys proxies that vanished, prunes their pairs,
   creates new proxies, then queries the trees only for proxies that moved and merges the hits into the pair
   cache. Pairs whose fat boxes stopped overlapping are dropped.
4. Each pair becomes a candidate for whichever side has a RigidBody. Candidate lists are sorted ascending,
   which reproduces the order the sweep visits edges in. `findCollisionsFromCandidates` then applies the same
   filters in the same order as the sweep (self, direct parent/child, layers, tight AABB overlap) and calls
   `collisions::intersects`.

Everything downstream (per-edge hit lists, response order, events, physics) is unchanged, so both modes are
meant to be bit-identical. `BroadPhaseEquivalenceTest` checks this over a falling pile and over a run with
objects removed and added mid-run.

The one deliberate divergence: an edge whose bounding box is not finite is left out of the tree and collides
with nothing that tick. The sweep would still test it.

## Toggle

The server reads the `ECS3D_BROADPHASE` environment variable at startup. `sweep` selects the old sweep;
anything else, or unset, selects the tree. The chosen mode is logged once under the physics category.
`CollisionSystem::setBroadPhaseMode` does the same from code and clears the tree on a change.

## Stats line

Every 250 ticks `CollisionSystem` logs one physics-category line, then resets its counters:

- `broadphase` - active mode.
- `candidates/tick` - average candidate entries after the broad phase (tree mode only; both sides of a
  dynamic-dynamic pair count).
- `narrow/tick` - average calls to `collisions::intersects` (tree mode only; the sweep path does not count).
- `reinserts/tick` - average tree leaf reinserts (tree mode only).
- `broadphase_us/tick` - microseconds per tick spent building inputs, updating the trees and building the
  candidate lists (tree mode only).
- `check_us/tick` - microseconds per tick for the whole of `checkCollisions`, responses included. This is the
  number to compare between modes.
- `pairs`, `static`, `dynamic`, `heights` - pair cache size, proxy counts per tree, and tree heights
  (static/dynamic), from the latest tick (tree mode only).

## Known issues / what the final implementation should do differently

- Proxy identity by `Collider*` plus `weak_ptr` is a stand-in. The final version wants explicit create and
  destroy hooks from `ObjectManager` (spawn, destroy, `objectComponentsChanged`, reparent, the rebuild done
  by scene stop) and stable collider ids instead of addresses.
- The edge list is still rebuilt, sorted, and every bounding box re-warmed every tick. The tree makes the
  sort unnecessary except for the response-order tie-break, which should get its own stable key.
- Every dynamic body still moves every tick (gravity plus an unconditional `Transform::move` that bumps the
  update id), and the narrow phase still runs on every cached pair every tick. A resting body only escapes
  its fat AABB if it drifts past the margin, so the tree saves broad-phase work but not narrow-phase work.
  Sleeping, and a GJK warm start keyed on the pair cache, are the real follow-up.
- Dynamic-dynamic pairs are still narrow-phased from both sides, and `findContact` is still recomputed in the
  serial response pass. The pair cache is where a once-per-pair contact result should live.
- A child collider's cached AABB goes stale when only its parent moves (the child's Transform update id does
  not change). The tree inherits that bug from the sweep.
- Siblings under one rigid-body parent are not excluded from colliding with each other (only direct
  parent/child is). A compound-body proxy filter should fix that.
- The margin (0.1) and displacement multiplier (4) are fixed constants, while object scale in this engine
  ranges from 0.25 to 100. A size-relative margin may work better.
- The tree could also back `SceneQueries::raycast` and `overlapSphere`, which are still brute force.
- Layer and mask filtering happens per candidate rather than when a pair is created, because a layer change
  does not move the proxy. The final version should treat a layer or mask change as a proxy refresh.
- Per tick the update allocates its input, candidate and scratch vectors, and the tree query allocates a
  small stack vector per call. Fine for a prototype; these should be reused members.
- The pair-cache prune and the sort after new hits are linear or n log n in the pair count every tick. A
  merge of the small sorted batch of new pairs into the cache would be cheaper.
- The equivalence test compares floats with exact equality. It assumes the physics pass is deterministic
  run to run on the same machine, which OpenMP reductions elsewhere could break.
