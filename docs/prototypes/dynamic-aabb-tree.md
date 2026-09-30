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

## Exact-preserving performance round

Everything below is bit-exact with the sweep path, which stays the untouched baseline: all of it lives on the
tree path, so `BroadPhaseEquivalenceTest` proves it. The one runtime knob is
`CollisionSystem::setContactCacheEnabled` (default on).

- Lean per-edge data. After the warm and sort, `buildEdgeInfos` copies plain data per edge (raw object and
  parent pointers, collider, has-RigidBody, layer, mask, a bounding box copy, trigger flag, geometry key).
  `updateBroadPhase` and `findCollisionsFromCandidates` read only that, applying the same filters in the same
  order, so the parallel region copies no `shared_ptr` for a rejected candidate.
- One GJK per contact. `collisions::collide` returns what `intersects` and `findContact` would, from a single
  GJK run. The parallel pass calls it for every candidate that passes the filters (triggers only need
  `intersects`) and stores the contact beside the hit. The response pass asks `contactFor`, which reuses it
  while both colliders' geometry keys (sum of the Transform update ids of the owner and every ancestor whose
  Transform is combined into its world placement) are unchanged, and otherwise calls `findContact` as before.
  The mesh a box collider caches is refreshed only when its own Transform id changes, which also changes the
  key, so a reused contact never skips a mesh rebuild the old code would have done.
- EPA: `Polytope` counts edges linearly instead of building a `std::map`, reuses scratch edge vectors, and
  reserves its vertex and face storage. Visit and append order is unchanged.
- Bounds: `Collider::computeBounds` is a virtual with the old six support queries as its default. The box
  override makes one pass over its vertices with `findFurthestPoint`'s exact selection rule; the sphere
  override computes the scaled radius and positions once and repeats the same float operations per axis.
- Server: `ServerApp` logs a `server` line every 250 ticks (see below).

## Sleeping

A settled group of bodies (an island) stops costing anything until something disturbs it. It is off by default
in the libraries (`CollisionSystem::setSleepingEnabled`, false) and only operates in tree mode. The server turns
it on unless the environment variable `ECS3D_SLEEP` is `0`; the choice is logged once.

- A body counts a rest tick per tick while something supports it (`!getNextFalling()`), its speed is under
  `linearSleepSpeed` and `angularSleepSpeed` (`sim/Sleeping.h`), and nothing is queued. An island is the
  union-find over awake dynamic bodies joined by non-trigger contacts with each other; static contacts do not
  join. It sleeps when every member has `ticksToSleep` rest ticks.
- Falling asleep zeroes velocity, records the body's geometry key (sum of the Transform update ids up the
  ancestor chain), and caches each edge's hit list for the tick (objects, their geometry keys, dynamic and
  trigger flags). Trigger hits are cached too, so a sleeper inside a trigger keeps its `stay` events.
- While asleep: `PhysicsSystem::fixedUpdate` skips the body (no gravity, no move), and the collision system
  skips its narrow phase and response, replaying the cached hit list so pairs keep producing `stay` events.
- Waking is by island: pending forces, non-zero velocity, a changed own geometry key, a cached hit whose object
  is gone or moved (or is now an awake dynamic body), or an awake body's narrow phase hitting a sleeper. Wakes
  from collision are collected per edge in the parallel pass and applied serially after it; the response pass
  still treats those bodies as asleep for that tick.
- `RigidBody` holds the four runtime-only fields (`m_asleep`, `m_restTicks`, `m_islandId`,
  `m_sleepGeometryKey`); `start()`/`stop()` reset them. Nothing is serialized, packed or replicated.
- Disabling sleeping, `reset()` and `setBroadPhaseMode()` clear the caches and wake every body on the next tick.
- Stats line additions (tree mode, sleeping on): `asleep_bodies`, `asleep_islands` (at the last tick),
  `fell_asleep/tick`, `woke/tick`, `sleep_skipped_edges/tick` (edges whose narrow phase was skipped).

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
- `contacts_parallel/tick`, `contacts_reused/tick`, `contacts_recomputed/tick` - contacts computed in the
  parallel pass (tree mode, cache on), reused by the response pass, and recomputed there (a moved body, a
  trigger pair, or the cache off). Reused plus recomputed is what the response pass asked for.
- `narrow/tick` now counts narrow-phase entries in both modes (the sweep counts calls to `intersects`, the
  tree counts candidates that reached the narrow phase), so the modes are comparable.
- Stage averages per tick, in microseconds: `gather_us` (edge collection), `warm_sort_us` (bounding box warm,
  sort and, in tree mode, the edge data build), `broadphase_us`, `narrow_us` (the parallel region),
  `response_us` (the serial response pass including its ordering) and `events_us`. They do not sum to
  `check_us`: gather happens before it and is reported separately.
- `pairs`, `static`, `dynamic`, `heights` - pair cache size, proxy counts per tree, and tree heights
  (static/dynamic), from the latest tick (tree mode only).

## Server stats line

Every 250 fixed ticks `ServerApp` logs one `server`-category line and resets: `objects` (average scene
object count), `scripts_us/tick` (script `variableUpdate` plus `fixedUpdate`), `physics_us/tick`,
`collision_us/tick` (the whole `CollisionSystem::fixedUpdate`), `events_us/tick` (collision event dispatch into
scripts), `broadcast_us/tick` (`broadcastStructuralChanges` plus `broadcastStateDelta`, per tick) and
`capped_iterations=N/M` - loop iterations that ran the maximum of 3 fixed steps, out of iterations that ran
any, which is the server falling behind.

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
- The contact cache is a per-tick, per-side cache. The real version should store one contact per pair in the
  pair cache and carry it across ticks, warm-starting GJK and EPA from last tick's separating axis and simplex.
- Dynamic-dynamic pairs are still computed from both sides (A to B and B to A). Halving that changes the
  response semantics and needs its own design.
- The serial response pass is inherently sequential in the current solver: each response moves transforms that
  later contacts read, which is also why a cached contact is revalidated by geometry key at use. A scalable
  solver would batch contacts into independent islands or colors.
- `Polytope::findContactManifold` and `boxContactPoints` still allocate (vectors per call).
- `std::weak_ptr::lock` in every `findFurthestPoint` call is an atomic operation per support query.
- `collide` now runs `findContact`'s EPA and manifold inside the OpenMP region, so an exception from it (a
  collider whose Transform vanished) would terminate instead of surfacing from the serial pass. Edges are
  gathered only for objects with a Transform, so it should not occur.
- A cached contact is valid only while nothing but Transform update ids changes a collider's geometry. Collider
  setters (offset, size, radius) do not bump a Transform id; they only run between ticks today. A collider
  generation counter would make the key complete.
- Reusing a contact does not skip any lazy mesh rebuild the old path would have made, but only because a stale
  mesh (child collider whose parent alone moved) is stale identically on both paths. Fixing that known issue
  needs the mesh key to include ancestors, and then the geometry key already accounts for it.
- Collision event dispatch was dominated by `ObjectManager::getObjectByUUID`'s linear scan. The prototype skips objects
  with no attached script, but the real fix is a uuid index in `ObjectManager` (maintained through add, remove,
  `reassignUUIDs`, unpack and restore), which also speeds up every `World.tryGet*` script binding.
- Sleeping thresholds (`linearSleepSpeed`, `angularSleepSpeed`, `ticksToSleep`) are untuned constants; the real
  version wants per-body overrides and tuning against the stacking scenes.
- Islands are rebuilt from scratch each tick by union-find; a persistent contact graph (the pair cache carrying
  contacts) would make this incremental.
- The state delta still replicates every object every tick even when asleep; skipping unchanged transforms is a
  replication follow-up.
- Wake detection relies on the Transform update id; a child collider's world placement moving only because its
  parent moved is caught by the ancestor-sum key, but anything that changes geometry without bumping an update
  id (the known child-cache staleness) is not.
- Sleeping only exists in tree mode.
- A static collider moved into a sleeping body that it was not touching when the island fell asleep is not
  noticed: the sleeper skips its narrow phase and only re-checks what it touched.
- A sleeping body whose collider is removed has no edge left to wake it, and stays asleep until a `reset()`.
- A moving trigger overlapping a sleeper wakes its island every tick its geometry key changes.
