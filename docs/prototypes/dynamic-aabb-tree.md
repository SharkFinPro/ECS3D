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

## Contact refresh (spike)

In the serial response pass, each response moves a body, which changes its geometry key, so most later
contacts involving it were recomputed with full GJK+EPA. Most of those moves are pure translations. With
refresh on, `contactFor` carries the cached contact along instead. Approximate, off by default
(`CollisionSystem::setContactRefreshEnabled`); the server enables it when `ECS3D_CONTACT_REFRESH=1`, and logs
the choice once. With it off, nothing changes and the equivalence tests still apply.

- The parallel pass also stores both colliders' world position, rotation and scale beside each cached contact
  (only with refresh on).
- On a key mismatch (and not a sphere-sphere pair, which is cheap to recompute exactly), `refreshContact`
  compares the current poses with the stored ones. Any change of rotation or scale falls back to the exact
  recompute. Otherwise, with `delta` the change of relative position and `n` the contact normal, the new depth
  is `depth - dot(delta, n)`; the point and manifold points shift by half the summed displacements; a depth of
  zero or less reports no contact. A tangential drift over `refreshMaxTangentialDrift` (0.05) or a normal
  motion over `refreshMaxNormalMotion` (0.25) falls back too. Both constants are untuned.
- Counters: `contacts_refreshed/tick` (carried along; not included in `contacts_recomputed`, so reused +
  refreshed + recomputed is what the response pass asked for) and
  `refresh_fallback_rot/drift/normal/sphere` per tick, the reasons a contact that could have been refreshed
  was recomputed exactly instead.
- Doing it properly is a persistent manifold that stores per-body local contact points (as Bullet's
  `btPersistentManifold` does) and refreshes them from both bodies' full transforms, rotation included, then
  drops points that drifted apart.

## Round 5: threads, more parallel stages, diagnostics, island-parallel response

Everything in this round is bit-exact with the serial tree path (and so with the sweep baseline, whose
`num_threads(6)` pragma is untouched). `ParallelResponseTest` checks it.

### Thread count

- `CollisionSystem::setThreadCount(int)` / `getThreadCount()`; library default 6, values under one clamp to one.
  Every tree-path OpenMP region uses it (each copies it into a local `const int threads` for `num_threads`).
- The server reads `ECS3D_PHYSICS_THREADS=<n>` (1 to 256); unset or invalid it uses
  `max(1, hardware_concurrency() / 2)`, a guess at the physical core count. The choice is logged once with the
  other startup lines, and every stats line carries `threads=<n>`.
- The default is a guess. OpenMP with more threads than physical cores (hyperthreads) often does not help, since
  the regions here are short and memory-bound; measure 6, 8, 12 and 24 on the target machine.

### What runs in parallel now

Tree mode only. In order of the tick:

- Bounding box warm (`getBoundingBox` on every edge). Safe because each edge is a distinct collider (component
  lookup walks to the parent only for `rigidBody`, never for a collider) and `getBoundingBox` writes only that
  collider's own caches (`m_boundingBox`, `m_transform_ptr` when expired, a box's transformed mesh) while only
  reading Transforms, parents included, through `getComponent` on an `unordered_map`. Nothing writes a Transform
  during the warm. The `shared_ptr` copies it makes are atomic.
- `buildEdgeInfos`: the vector is sized first and each iteration writes its own slot. `EdgeInfo::body` is now
  always set (the sleeping-only fields `asleep`/`islandId` still are not).
- Broad phase: proxy moves, creates and destroys stay serial. The queries for the moved proxies then run in
  parallel into one vector per moved proxy (the trees are read-only by then) and merge serially before the
  existing sort and unique, so the pair cache is identical.
- Narrow phase (already parallel; now uses the setting).
- Still serial: the edge sort, `responseOrder`, `recordCollisionEvents`, the sleeping pass, candidate list build.

### Island-parallel response (`setParallelResponseEnabled`, default off)

The server enables it with `ECS3D_PARALLEL_RESPONSE=1` and logs the choice once. Tree mode only; with it off
nothing changes.

Why it is exact: a response changes only the body it resolves and, when the other side is dynamic, that body,
and only reads static geometry. So bodies that share no hit share no state. `respondInParallel` builds a
union-find keyed by the resolved `RigidBody*` (a child collider's body is its parent's), joins the two bodies of
every hit where the other side has a body, then lists each component's edges in the order the global
`responseOrder` gave them and resolves the components on separate threads (largest first, dynamic schedule, each
component serial). Two components never touch each other, and within a component the order and the arithmetic
are the serial ones, so the result is bit-identical.

Two deliberate widenings beyond "non-trigger hits between dynamic bodies", both for safety:

- Trigger hits join too. `handleCollisions` measures the contact of every hit (to score them) before skipping
  triggers, and a contact measurement can regenerate the other collider's mesh cache from that body's
  Transform, which another thread may be moving.
- A body joins the body of its nearest ancestor that has one. A nested body's world placement reads its
  ancestors' Transforms, which the ancestor body's response writes.

Audit of state touched during a response that is not the pair's own (all checked against the source):

- `PhysicsSystem.cpp` has no static or function-local static state, no globals beyond `constexpr` constants,
  no `Log` calls and no lazily initialized caches. It throws `runtime_error` only on a null `other`, which
  cannot happen from here (an exception escaping an OpenMP region would terminate).
- `m_counters` updates in `contactFor`/`handleCollisions` and `m_contactsRefreshedTotal`: made per-task. Both
  functions are now `const` and take a `ResponseCounters&`; each component keeps its own and they are summed
  after the loop.
- Reads of `m_collisionEdges`, `m_edgeInfos`, `m_cachedContacts`, `perEdgeCollisions`: read-only during the pass.
  Each edge belongs to exactly one component, so no `m_cachedContacts` slot is shared.
- Sleeping bookkeeping runs after the response, not during it. An asleep edge is skipped as before, but a
  sleeper that an awake body touches is mutated by that body's response, so it is joined to it.
- Collider caches: a static collider's mesh is regenerated only when its own Transform update id changes, and
  statics do not move mid-tick and were warmed at the start of the tick, so response reads of a static collider
  never write. A dynamic collider's mesh may be regenerated by a response, but only by a thread holding its
  component. `m_transform_ptr` is set during the warm for every edge.
- Transform and `geometryKeyOf` reads walk ancestor chains: covered by the ancestor join above. A body with no
  body anywhere up its chain has a chain nobody writes.
- `shared_ptr` copies (`getComponent`, `other->getComponent`) are atomic reference count updates on shared
  control blocks; nothing else is shared.

Nothing had to be refused and nothing is inexact. The cost of building components is serial (`resp_setup_us`);
the per-hit body lookup (`hit_graph_us`) is parallel.

### Diagnostics on the stats line

- `resp_recompute_us`, `resp_refresh_us`, `resp_physics_us` (per tick): time in exact recomputes inside
  `contactFor`, in refreshes (the refresh attempt, whether or not it fell back), and in
  `PhysicsSystem::handleCollision`. Under the parallel response they are sums over all threads, so they can
  exceed `response_us`, which stays the wall time of the pass. The rest of a serial pass is the difference
  (mostly reuse checks, scoring and sorting).
- `hit_graph_us` (per-hit body lookup, shared by the island diagnostics and the parallel response),
  `diag_us` (island and sleep-blocker analysis) and, with the parallel response on, `resp_setup_us`,
  `resp_components/tick`, `resp_largest_component_edges/tick`. Diagnostics can be switched off with
  `setDiagnosticsEnabled(false)`; their cost is in `hit_graph_us` and `diag_us`, and the sleep-blocker part is
  inside `events_us`.
- Island structure of this tick's hits: `islands/tick`, `largest_island/tick` (average), `largest_island_max`
  (over the reporting interval) and `bodies_in_islands_ge_64/tick`. Union-find over dynamic bodies joined by
  any non-trigger hit where both sides have a body; static contacts do not join, and asleep edges are left
  out. If `largest_island` is close to the awake body count, an island-parallel response has nothing to split.
- Why awake bodies do not sleep (sleeping on): `awake/tick` and, as per-tick counts of awake bodies at the end
  of the tick, `no_sleep_unsupported/fast/spinning/forces/island_blocked`. The criteria are counted
  independently, so one body can appear in several. `island_blocked` means the body itself rested for
  `ticksToSleep` ticks but its island did not sleep. Then two histograms over supported awake bodies:
  `supported_speed_hist(<.001/<.004/<.01/<.02/<.05/>=.05)` in per-tick displacement units and
  `supported_spin_hist(<1/<3/<10/<30/>=30)` in degrees per second.
  How to read them: if most supported bodies sit in the first bucket or two and `island_blocked` is high, the
  thresholds are fine and one restless body keeps a big island awake. If they cluster above the thresholds
  (say 0.004 to 0.02, or spin over 3), the stacked solver never quite settles at these values and either the
  thresholds or the solver's resting behavior has to change.

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
- The physics thread default (half of `hardware_concurrency`) is a guess at the physical core count, and the
  library default of 6 exists only to keep tests and old behavior unchanged. With OpenMP, using hyperthreads as
  well may not help, and each region pays fork/join cost, so short regions (the warm, edge data) may gain little.
- The parallel response's component build is serial (`resp_setup_us`) and unions every hit, triggers included,
  plus ancestor bodies; a scene that is one connected pile makes it a single component and a pure overhead.
- A trigger's contact is still measured (and its result used only to score hits) in every response; skipping it
  would cut work and remove the one reason triggers join components.
- The exception-safety note for the narrow phase applies to the parallel response as well: an exception in a
  response task terminates the process instead of surfacing from the serial pass. None is expected.
- A static collider moved into a sleeping body that it was not touching when the island fell asleep is not
  noticed: the sleeper skips its narrow phase and only re-checks what it touched.
- A sleeping body whose collider is removed has no edge left to wake it, and stays asleep until a `reset()`.
- A moving trigger overlapping a sleeper wakes its island every tick its geometry key changes.

## Recommendation for the real implementation

- Broad phase: solved. The dynamic tree plus a persistent pair cache reproduces the sweep exactly and is cheap.
- Narrow phase: parallel and cached. One GJK per contact in the parallel pass, reused while geometry is unchanged.
- Event dispatch needs a uuid index in `ObjectManager`; the linear `getObjectByUUID` scan dominates otherwise.
- The scaling wall is the serial move-and-remeasure solver: every response moves a body and invalidates the
  contacts behind it. Refresh (round 4) only shaves the recomputes; it does not remove the serial dependency.
- The real fix is a solver over cached contacts: sequential impulses on cached manifolds, position correction
  that does not re-run GJK/EPA, and independent contact groups (islands or colors) solved in parallel. That is a
  physics rewrite, and it has to re-home stack load hand-off, friction holding, `layFlush` and landing on a face.
- The island-parallel response (round 5) cannot help a single connected pile: the pile is one component and
  runs serially however many threads there are. It only pays when a scene is many separated groups. Splitting a
  connected pile needs a solver that can run contacts of one island concurrently: graph coloring of the contact
  graph, or a Jacobi / parallel Gauss-Seidel formulation with a fixed iteration count. Both change results, so
  they belong to the solver rewrite above rather than to this prototype.
- Read the round 5 diagnostics before tuning anything: `largest_island` says whether splitting can help at all,
  and the sleep-blocker counts and speed histograms say whether the few sleepers are a threshold problem or a
  solver that never settles a stack.
- Measured numbers with refresh on and off: TODO (developer to fill in).
