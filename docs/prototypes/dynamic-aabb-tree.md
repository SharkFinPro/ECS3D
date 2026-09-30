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
  `resp_components/tick`, `resp_largest_component_edges/tick`. From round 6 the per-contact timers and the
  island and sleep-blocker analysis are behind `setDetailedTimingEnabled` (see Round 6); their cost is in
  `diag_us`, and the sleep-blocker part is inside `events_us`.
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

## Round 6: trimming the serial response pass

Measured before this round (about 2160 bodies, 20 threads, refresh on, parallel response off, per tick):
`response_us` 12,800-17,700, of which `resp_recompute_us` 4,000-5,400, `resp_refresh_us` 9-17 and
`resp_physics_us` 4,100-5,700. That left 4,700-6,600 us of the pass unattributed. Everything below is exact:
the same floating-point work in the same order, so the existing equivalence tests are the guard.
None of it has been built or measured yet.

### Attribution and the detailed timing switch

- New per-contact timers: `resp_keys_us` (the geometry key comparison in `contactFor`), `resp_score_us` (the
  multi-hit scoring loop and stable sort, less the time `contactFor` itself accounted for) and `resp_lookup_us`
  (resolving the components a response needs, and the trigger test). New always-on coarse timer
  `resp_order_us`: `responseOrder` (a sort over every edge with hits) runs inside `response_us` and was part of
  the unattributed time.
- `CollisionSystem::setDetailedTimingEnabled(bool)`, library default off, gates every `resp_*` per-contact timer
  (the existing three included) and the per-tick island and sleep-blocker analysis (`diag_us`, `islands/tick`,
  the sleep-blocker counts and histograms). It replaces `setDiagnosticsEnabled`. Off, the response hot path reads
  no clock per contact (`ScopedMicros` takes an enabled flag); the per-stage timers stay on. The stats line
  reports `detailed_timing=on|off` and omits the entries that are off.
- The server reads `ECS3D_COLLISION_DIAGNOSTICS`: `0` turns detailed timing off, anything else, or unset, leaves it
  on (the prototype still wants the data by default). It is logged once at startup. For the cleanest
  `response_us`, run with `ECS3D_COLLISION_DIAGNOSTICS=0`.

### Fewer lookups per contact

- `buildEdgeInfos` now records per edge the raw `Transform*` chain `geometryKeyOf` sums (the object's own, then
  each ancestor up to the first without one; inline capacity four, a deeper chain falls back to
  `geometryKeyOf`), the Transform and Collider of the rigid body's owner (a child collider's body belongs to
  its parent), and reuses the edge's own body and collider pointers. `keyOf(EdgeInfo)` returns exactly what
  `geometryKeyOf` returns, without a `getComponent` per level. `contactFor` and the sleeping records use it.
- Each edge has a `m_hitEdges` list aligned with its hit list (the hit's own edge index), so a hit's collider,
  trigger flag, body and key chain come from `m_edgeInfos`, not `getComponent` + `dynamic_pointer_cast`. The
  `shared_ptr<Object>` hit list is unchanged. The sweep path leaves `m_hitEdges` empty and keeps the old lookups.
- The response loops take the body from `EdgeInfo::body` instead of a lookup, `ScoredContact` holds a pointer to
  the hit instead of a `shared_ptr` copy (an atomic increment per hit), and a re-measured contact is read through
  a pointer instead of copying the optional `Contact`.
- `PhysicsSystem::Parties` (raw pointers: the body owner's Transform and Collider, the other side's rigid body,
  that body owner's Transform, and the other object's Collider) is threaded through `handleCollision`,
  `pairOf`, `stopSpinIntoSupport` and `comeToRest`, which each looked these up again before. A new
  `handleCollision` overload takes them pre-resolved; the existing overloads call `resolveParties`, which
  resolves exactly what each call site did. The rare edge-landing helpers (`supportFaceToward`, `restingFace`,
  `layFlush`, `turnsFlatThisTick`) still take the `shared_ptr<Object>` and look up what they need.

### Parallel integrate

`PhysicsSystem::fixedUpdate` integrates in parallel when `PhysicsSystem::setThreadCount` is above one (default
one, so library and tests are unchanged; the server passes the same physics thread count), at least 64 bodies
are active, and no active body has an ancestor that owns a rigid body. A nested body's integrate reads its
ancestors' Transforms (`getPosition`, `setWorldRotation`), which a body ancestor's integrate writes; the walk
is conservative (any ancestor with a rigid body, awake or not) and, if it fires, the pass stays serial that
tick. The gather and wake passes stay serial; only "apply queued forces, then integrate" per body fans out.
`PhysicsSystem::lastIntegratePath()` reports `serial`, `serialNested` (the guard blocked it) or `parallel`.

Audit of shared writes on that path: `integrate`, `applyVelocityChange` and `worldInverseInertia` write only the
body's own `RigidBody` and `Transform` (`ComponentVariable::set`, `++m_updateID`, plain fields) and read
ancestors' Transforms; `PhysicsSystem.cpp` has no statics, no `Log` calls and no caches on it (the two file-level
variables are the thread count and the last path, written outside the parallel loop); `getParent()` and
`getComponent` are reads plus atomic reference count updates. Nothing else was found.

### Collision events

`recordCollisionEvents` no longer flattens every hit into 32-byte pairs and comparison-sorts them. From
`m_hitEdges` it ranks the uuids of the edges that take part (a sort over at most the edge count, dense so equal
uuids share a rank), reduces each hit to a `(rank, rank)` pair of small integers, buckets them by the smaller
rank with a counting pass, sorts and dedupes each bucket, and emits the pairs in bucket order. Rank order equals
uuid order, so the list is exactly what sorting and uniquing the pairs gives. The three set operations against
last tick's list are unchanged. Without aligned hit lists (sweep) the old code runs.
The collision hit-body lookup for the island diagnostics and parallel response (`collectHitBodies`) reads the same
edge data.

### Tests

`PhysicsOverheadTest.cpp`: detailed timing on vs off is bit-exact over the clump scene (refresh and sleeping on);
tree vs sweep is bit-exact through a hierarchy deeper than the inline key chain; 1 vs 8 integrate threads is
bit-exact over 240 free bodies for 200 ticks (and over the clump scene with refresh and sleeping), with the
last path asserted to be `serial` and `parallel`; a scene with nested bodies must report `serialNested` and stay
bit-exact; a scene under the body threshold stays serial.

## Round 7: a reproducible benchmark and sleeping experiments

Stress runs were not comparable: each loaded a different random Scene 3, and scaling it by duplicating objects in
the editor starts every copy inside its original, so it measured an explosion rather than a pile. Round 7 makes
the scene repeatable and scalable, adds a headless benchmark, and adds switches to test why a slowly settling pile
sleeps so little.

### Scene 3 options

`buildDefaultProject` takes a `Scene3Options` (seed, grid size, layer count, overlap-free). The defaults are the
built-in scene exactly: grid 6, 15 layers, spacing 5, seeded from `std::random_device`. A seed also makes every
Scene 3 object uuid repeatable (drawn from the seed), because uuid order decides pair and event order, so two
builds of one seed are identical JSON. Overlap-free widens the spacing to 6.8 so no two bounding boxes start
overlapping (the largest body, turned to its worst angle, plus both jitters); grid 12 and 15 layers gives 2,160
bodies clear of each other. The ground is 200 by 200, which covers grid 12.

The server reads `ECS3D_SCENE3_SEED` (0 to 4294967295), `ECS3D_SCENE3_GRID` (1 to 64) and `ECS3D_SCENE3_LAYERS`
(1 to 200). An invalid value is warned about and ignored. When any of the three is set the layout is overlap-free
(the spacing changes from the built-in 5), and one line logs the result.

### ECS3DPhysicsBench

A headless console executable in `source/apps/bench/`. It links only `ECS3DData`, `ECS3DSim` and `ECS3DLog`, and
compiles `../server/DefaultProject.cpp` in directly, as the test target does: no CLR, scripts or networking. It is
`EXCLUDE_FROM_ALL` and not a CTest test, so build it on request:

    cmake --build cmake-build-ecs3d-release --target ECS3DPhysicsBench

It builds the default project with the Scene 3 options, loads it the way the server does, selects Scene 3, starts
it, and steps `PhysicsSystem::fixedUpdate` then `CollisionSystem::fixedUpdate` at dt = 1/50 back to back. The
usual 250-tick stats lines print through a `ConsoleSink`; the last line is a `SUMMARY` with the flags, body count,
wall time, mean/median/p95/max per-tick microseconds (physics plus collision) and the final asleep count.

    ECS3DPhysicsBench --grid 12 --layers 15 --ticks 3000 --seed 1

Flags (each takes a value): `--ticks N` (1500), `--seed S` (1), `--grid G` (6), `--layers L` (15), `--threads T`
(half of hardware_concurrency), `--broadphase sweep|tree` (tree), `--refresh 0|1` (1), `--sleep 0|1` (1),
`--parallel-response 0|1` (0), `--diagnostics 0|1` (0), and the sleep flags `--sleep-linear F`, `--sleep-angular F`,
`--sleep-ticks N`, `--sleep-support falling|contact`, `--sleep-mode island|grounded`. Every run is seeded, so
overlap-free spacing is always on, and the same flags give the same scene.

### Sleeping experiments

All are switches with the old behavior as the default; the server takes them from the environment, the bench from
flags, and the stats line prints `sleep_mode`, `sleep_support` and the thresholds.

- Thresholds: `CollisionSystem::setSleepThresholds(linear, angularDegrees, ticks)`; `ECS3D_SLEEP_LINEAR`,
  `ECS3D_SLEEP_ANGULAR`, `ECS3D_SLEEP_TICKS`. Defaults are the `Sleeping.h` constants.
- Support test, `ECS3D_SLEEP_SUPPORT=falling|contact`. `falling` (default) is `!getNextFalling()`. `contact` counts
  a body as supported when this tick at least one non-trigger contact, against static geometry or another body,
  pushes it out with a normal of y >= 0.5 (it rests on something). It reads the contact the parallel narrow phase
  cached for that hit (`verticalRole`), not the one the response pass measured, because the response pass does
  not keep its contacts; that contact is from before this tick's responses, which does not change which way it
  points in practice. With the contact cache off no hit has a contact, so nothing counts as supported.
- Sleep mode, `ECS3D_SLEEP_MODE=island|grounded`. `island` (default) is the all-or-nothing union-find.
  `grounded` puts a body to sleep when it is eligible (rest ticks reached, support test passed) and every body it
  rests on this tick is static or already asleep, sweeping until a pass adds nobody, so a pile sleeps from the
  bottom up. Deviations from the brief, on purpose:
  - Every sleeping body gets its own island id, not its support group's. A shared id would wake the whole stack
    whenever any member is disturbed, which is what island mode does. Locality comes from an explicit closure
    instead: each sleep record remembers which hits its body rests on (`SleepHit::supportedBy`), and
    `wakeUnsupportedSleepers` wakes every sleeper resting on something awake or gone, upward until none is left,
    every tick after the wake requests. A body the physics pass wakes (force, velocity, moved) therefore also wakes
    what rests on it, that same tick.
  - The awake-neighbor wake rules are relaxed. An awake body touching a sleeper wakes it unless the toucher moves
    slower than `gentleContactSpeed` (0.02 per tick) and the contact is a real one, and a sleeper only wakes for
    an awake or moved dynamic hit when it rests on that hit. Anything else, a lateral neighbor included, has to
    push the sleeper for real, which shows as its own geometry key changing. Without this a sleeper next to a
    creeping awake neighbor would be woken the tick after it fell asleep. Static hits keep the old rules.

To compare, run the same flags with one switch changed and read `asleep_bodies`, `fell_asleep/tick` and
`woke/tick` (equal means churn) on the stats lines, and the summary's `final_asleep`, for example:

    ECS3DPhysicsBench --grid 12 --layers 15 --ticks 3000 --seed 1 --diagnostics 1
    ECS3DPhysicsBench --grid 12 --layers 15 --ticks 3000 --seed 1 --diagnostics 1 --sleep-support contact
    ECS3DPhysicsBench --grid 12 --layers 15 --ticks 3000 --seed 1 --diagnostics 1 --sleep-support contact --sleep-mode grounded

With diagnostics on, the blocker counts use the selected support test. Nothing here has been run: the numbers are
for the developer to fill in.

### Tests

`DefaultProjectTest` gains a seeded Scene 3 case (same seed, same JSON; different seed, different JSON) and a
grid 12 / 15 layer case (2,160 bodies, no two conservative bounding boxes overlapping). `SleepingTest` keeps its
cases and gains: the contact support test sleeping a grounded box and not a box in flight; grounded mode sleeping a
stack of five bottom-up (no body asleep while the one below is awake, all asleep within the bound), one island
per body, the falling support test in grounded mode; a box dropped on a grounded stack waking its top (how far the
wake spreads is recorded as test properties, not asserted, since it is what the experiment measures) and the stack
settling again; and removing the ground waking a whole grounded stack in one tick.

## Toggle

The server reads the `ECS3D_BROADPHASE` environment variable at startup. `sweep` selects the old sweep;
anything else, or unset, selects the tree. The chosen mode is logged once under the physics category.
`CollisionSystem::setBroadPhaseMode` does the same from code and clears the tree on a change.
`ECS3D_COLLISION_DIAGNOSTICS=0` turns the detailed timing and diagnostics off (see Round 6); anything else, or
unset, leaves them on.

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
- Grounded mode reads support from the parallel pass's cached contacts, so a contact that has rotated past the
  0.5 normal threshold under this tick's responses still counts as it was measured, and a hit with no cached
  contact (cache off, trigger) never counts as support.
- Grounded mode gives every sleeper its own island id, so `asleep_islands` equals `asleep_bodies`, and each wake
  re-scans every sleep record (`wakeUnsupportedSleepers`, linear in records and hits per tick while grounded).
- In grounded mode a sleeper is woken only by a contact that pushes it (its own geometry key changes), by an awake
  toucher faster than 0.02 per tick, or by losing what it rests on. A slow push that moves it less than the
  geometry key notices would not wake it; that has not been measured.
- Scene 3's `overlapFree` layout changes the spacing (5 to 6.8), so seeded runs are not the same pile as the
  built-in scene, only a repeatable one.

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
- In the real implementation, systems should hold resolved component pointers or indices per body for the
  duration of a tick instead of repeating `getComponent` + `dynamic_pointer_cast` lookups (an `unordered_map`
  find, a cast and an atomic reference count update each). Round 6 gets some of this back by caching raw
  pointers per collision edge, but the same lookups are repeated in every system (physics, scripts, render,
  replication), so it is an ECS storage concern: dense per-type component arrays with a stable per-tick body
  index, not a collision-only fix. A uuid index in `ObjectManager` belongs to the same change.
- Measured numbers with refresh on and off: TODO (developer to fill in).
