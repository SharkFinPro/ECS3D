# Physics and collision

The root `AGENTS.md` summarizes this topic and links here. Read this file before changing the code it describes,
and update it in the same change when that behavior moves.

## ECS3DSim

`ECS3DSim` holds `PhysicsSystem` (integration and forces), `CollisionSystem` (the sweep, the islands and the
contacts handed to the solver), `ContactSolver` (the response) and the narrow phase under `collisions/`.

### Units and the step

- Linear velocity is displacement per tick. Angular velocity is a world-space axis in degrees per second, so
  convert it to radians per tick before combining the two; `applyVelocityChange`, `applyImpulse` and
  `CollisionSystem::fixedUpdate` take `dt` for that.
- Gravity adds `g * unitsPerMeter * dt * dt` to the velocity each tick, so a fall covers the same distance at any
  tick rate. A body's gravity is in meters per second squared, and the content is authored at
  `PhysicsSystem::unitsPerMeter` (5) world units per meter, so the server's default gravity falls at 49.05 world
  units per second squared.
- The inertia tensor is per unit mass and comes from the body's collider shapes in world geometry (see
  [Inertia](#inertia)), so a velocity change moves and turns a body alike whatever its mass while an impulse is
  divided by the mass for both.
- Orientation turns by composing a rotation onto it rather than by adding to the Euler angles, and the result is
  written back through `Transform::setWorldRotation`, since the rotation it turned is combined with the parent's.
- **The step order.** `PhysicsSystem::fixedUpdate` first moves each body by the velocity the last tick's solve left
  it, then adds the tick's queued forces and gravity to that velocity. `CollisionSystem::fixedUpdate` runs next: it
  finds the contacts, solves them and separates the bodies. The solver so sees a force before it moves anything,
  and what a support or friction holds never moves at all. A force acts on the position one tick after it is
  queued.
- Spin below `PhysicsSystem::restAngularSpeed` does not turn a body, and every tick scales the angular velocity by
  0.99. Nothing damps linear velocity, so a body in flight keeps its horizontal speed.
- Whether a body is falling, and the normal of what it rests on (`RigidBody::getSupportNormal`), are what the
  previous tick's solve recorded: integration hands each tick's record on to the next and clears it. The support
  normal is the flattest upward contact normal that carried load, world up when there was none.

### Broad phase and islands

`CollisionSystem` sweeps and prunes in minX order. Each collider's bounds are stretched by its body's velocity, so a
pair that meets within the tick is found before it overlaps. Each edge scans forward only and tests a pair once,
from its lower-indexed edge, when either side is dynamic. A pair whose colliders resolve to the same rigid body, or
where one object is an ancestor of the other, is skipped before the narrow phase, and so is a pair whose layers do
not collide.

The narrow phase is not symmetric in its arguments, so each dynamic side asks `collisions::intersects` with itself
first and keeps its own answer and GJK's final simplex (`OverlapWitness`). A pair neither side sees overlapping is
asked whether it `approaches`: with the two sweeps against each other, and with each alone against the other
standing still, since a body about to be stopped by its own support no longer moves with what falls onto it.

An island is the bodies linked by dynamic-vs-dynamic hits or by one body's hierarchy; static colliders link
nothing. A trigger pair is recorded for events and gets no response. Each island's pairs are ordered by the lower
of their two bounding-box minY values (the bounds stretched by the tick's sweep), lowest first, so the solver's passes run up and down a stack and the order
does not depend on where along x a body sits. With OpenMP the narrow phase runs per sweep edge and the solve per
island, each island on one thread, so results match a serial pass.

For each pair of an island `CollisionSystem::solveIsland` asks `collisions::findContact` for the contact, the way
the sweep saw the pair: from the overlap witness for a pair that overlaps, and with the approach sweeps, in the
order above, for one that approaches. A pair that only touches has no depth to measure, so it is asked for pressed
0.02 together along the line between the colliders. `liftOutOfStatic` then moves any body deeper than
`collisions::contactMargin` into static geometry out to half that margin, deepest contact first, and measures that
body's other contacts again, each with the sweep or press it was found with, so a pair about to meet is still
stopped where it meets.

### The contact

A `collisions::Contact` holds the translation that moves the first collider out (`minimumTranslationVector`), up to
four points and a depth for each. `advance` is how much of the depth the first collider's sweep accounts for, and
`overlap()` is the rest: how far the shapes are into each other now, negative while they are still apart.

- A box or hull face manifold also reports the points of the other face within `contactMargin` (0.1) short of the
  reference face, with a negative depth. A face about to land flat then has its late corners stopped where they
  arrive. The deepest point takes the contact's overlap and the others keep their distance from it.
- For a box or hull pair resting face on face the normal is snapped to the reference face's own
  (`Polytope::faceOnDirection`), so it does not carry EPA's rounding.
- A swept query stretches the first collider's `SupportShape` along the sweep. Round pairs answer it from their
  closest points and the sweep's closing distance (`findRoundApproach`).
- The shortest way out of a sweep through something thin can be on through its far side. When the normal points
  within 60 degrees of the sweep and the shape began the tick no more than `contactMargin` into the other one
  along its motion, the pair is stopped along the motion itself, with the distance left before they meet as a
  negative overlap. A shape already further in did not sweep through anything, and keeps the contact EPA gave it.

### The solver

`ContactSolver::solve` resolves one island's contacts together (`ContactSolver.h`). Every contact point is a
constraint along the normal and two tangents, each keeping the total impulse it has applied so a later pass can
take some back. Static geometry is one body with no inverse mass. In order:

1. **Bounce.** Pairs that bounce trade their closing speed first, one pair at a time and again while any is still
   closing at `minBounceSpeed` (two units a second) or more, up to 16 sweeps. A ball striking a row of touching
   ones so hands its speed along and sends the last one off. Only a point that touches, or reaches the other shape
   this tick, bounces.
2. **Warm start.** Each pair starts from the impulses it ended the last tick with (`CollisionSystem`'s contact
   cache, keyed by the collider pair), where the normal still agrees and a point is still about where it was. A
contact of one point both ticks keeps its impulse wherever the point has moved.
3. **Velocity passes.** 32 passes over the constraints, alternating direction. Each resists rolling, then holds
   sliding, then stops the points closing, so the normal impulses have the last word. A point short of the other
   shape may close the distance it has left this tick and no more.
4. **Settle.** A pass up from the ground stops each body on the one holding it up as on static geometry, friction
   included, for contacts within 45 degrees of vertical. The impulse joins the contact's total, so the next tick
   starts with the body below carrying it.
5. **Separation.** Four passes move the bodies out of overlap, 0.8 of what is past `penetrationSlop` (0.0005) a
   tick, as a push kept apart from the velocity so it adds no speed, and the settle pass runs again on the pushes.
   A resting pair stays the slop into each other, so its contact is an overlap the narrow phase measures.
6. **Finish.** Each contact that carried load records its support (see the step above), the impulses go to the
   cache, and velocities and pushes are written back. A body held on a face (three points or more) or on a rolling
   contact has spin within `restAngularSpeed` of its support's dropped, so it comes exactly to rest.

What the constraints are made of:

- **Friction** is Coulomb: at each point it holds up to the coefficient times that point's normal impulse. The
  coefficient is the geometric mean of the two bodies', and the body's own against static geometry.
- **Rolling resistance** applies when either collider is a sphere or a capsule: an angular impulse up to a tenth of
  the friction coefficient times the shape's world radius times the load. A capsule lying along a two-point contact
  rolls only about its own axis.
- **Restitution** applies between two bodies only; what lands on static geometry does not bounce. It is full for a
  body in flight or one struck level with its support, and falls linearly to none as the hit presses a resting
  body into its support, reaching zero at 45 degrees. The pair takes the lesser of its two sides.
- **Shares.** A resting body pressed into its support by a heavier one, within 45 degrees of the support normal,
  takes only the share of each impulse that the mass ratio gives it (`shareTakenBy`), as if it weighed what
  presses on it; its support takes the rest. A stack then settles as one of equal bodies does whatever its masses.

**Static geometry.** The static body is the island's slot zero (`staticSlot`). It never moves, so the solver leaves
it out of relative velocities and pushes and gives it no impulse. That changes no result: its velocity, spin and
push stay exactly zero either way.

**Precomputed turns.** This is the one place the solver's arithmetic is not the plain formula, and it is not bit for
bit with it. An impulse `j` along a direction `d` at arm `r` turns a body by `I^-1 (r x (j d))`. Each `SolverPoint`
computes `I^-1 (r x d)` once per tick for its normal and both tangents, on both sides (`normalTurn`,
`tangentTurn` and the `other` versions), and the velocity passes then add `j` times that, through `applyTurning`.
In exact arithmetic the two are equal. In floats the multiply happens in a different order, so the spin each pass
leaves differs in its last bits.

- **What it changes:** individual runs, not behavior. A pile settles the same way and to the same tolerances, but a
  chaotic sequence (a tower collapsing, a ball rolling on a ridge) amplifies the rounding, so a recorded run does not
  replay exactly against one made before the change. The suite compares physics with tolerances, so it is unaffected;
  a test that pins an exact float from the velocity passes would not survive it.
- **Where it applies:** only the 32 velocity passes (relax and friction), where nearly all impulses are applied.
  Bounce, warm start, settle and separation still turn each impulse through the inertia.
- **Why:** on the Terrain Basin scene the velocity passes were a third of the single-threaded tick, and the
  matrix multiply in each impulse was the largest part of them.
- **To undo it:** have `relax` and `holdSliding` call `apply` with the impulse vector again and drop the turn fields.
  Nothing else depends on them.

### Sleeping

A pile at rest is still solved every tick for contacts that change nothing, so bodies that have stayed at rest
long enough sleep. The state is runtime only on `RigidBody` (`isAsleep`, its sleep group, its rest count), never
saved or replicated.

- **Falling asleep.** After the solve, `CollisionSystem::sleepRestingIslands` looks at each solved island. A body is
  at rest when it is supported this tick, steadily held (`RigidBody::isSteady`, which the solver writes: on a face of
  three points or more or a rolling contact, or at three points or more between its loaded contacts, as a box with an
  edge on the ground leaning on another is; not balanced on one edge or corner), slower than `sleepSpeed` (0.05 units
  per second) and turning slower than `sleepAngularSpeed` (1 degree per second). When every body of an island has
  been at rest for `sleepSeconds` (0.5) in a row, the whole island sleeps as one group, with its velocities zeroed.
  One restless body keeps its island awake.
- **Asleep.** `PhysicsSystem` does not move or accelerate a sleeper. The sweep does not test a pair with no awake
  body in it (two sleepers, or a sleeper and static geometry): the pair is recorded as `sleeping`, is not solved,
  keeps last tick's touch for the collision events (so a resting pair keeps reporting stay), and its cached impulses
  are kept for the warm start when it wakes.
- **Waking.** Any of these wakes the body's whole group:
  - a queued force, or a change of velocity, angular velocity, mass or gravity (the `RigidBody` setters);
  - a move of its transform from outside (`PhysicsSystem` compares the world update id against the one at sleep);
  - a change of its own collider's bounds;
  - any change to the static colliders' bounds or a terrain's revision (`wakeChangedSleepers` hashes them; it
    wakes every sleeper, since edits are rare and a pile may rest on what changed);
  - an awake body touching or about to meet it (`wakeTouchedSleepers`, after the narrow phase). The woken bodies'
    untested pairs are then tested as the sweep would have, which may reach more sleepers, before the islands are
    built, so the woken pile is solved with all its contacts that tick.
- **Catching up.** A sleeper `PhysicsSystem` passed by this tick is marked (`hasSkippedStep`). Woken later in the
  tick it takes `PhysicsSystem::catchUp`: the support bookkeeping and gravity the step would have given it. It moves
  nowhere, since it had no velocity, so it falls from the tick it wakes, as an awake body would.

What it changes: a resting pile stops creeping by float noise, and bodies that would have kept drifting slower than
the thresholds stop. On the Terrain Basin benchmark (`BasinBenchmarkTest`) about half the bodies are asleep after
20 seconds. Most of the awake ones are held in islands by a few that lean on an edge or still roll.

### Narrow phase

`NarrowPhase.h`'s `findContact`, `intersects` and `approaches` are the entry points. GJK and EPA query each
collider through a `SupportShape` refreshed once per call, and `findContact` starts EPA from the sweep's witness
simplex while neither collider's world update id or geometry revision has moved since. A pair of spheres and
capsules skips GJK, since a sphere is a capsule with a zero-length core segment. That pair is solved from the
closest points between the two core segments, treated as two spheres. Parallel overlapping capsules report two
manifold points. Every other pair goes through GJK/EPA, where a capsule's contact feature is its cap points facing
the other shape, clamped onto a box's footprint.

EPA grows its polytope in double precision, winding each new face from the horizon edge it replaces. The closest
face's distance bounds the depth from below, and the support along its normal from above, since moving by that
support distance separates the pair along that normal. EPA converges when the two agree to within a few float ulps
of the support point, and reports the best measured support translation: an exit short of convergence (a repeated
vertex, a face with no reliable normal, the iteration cap) measures every remaining face normal and takes the
least. The answer therefore never depends on which of several equally furthest corners a support function returns,
which `EpaConvergenceTest` pins against an exact separating-axis depth. A flat GJK tetrahedron, which touching
shapes give, starts from the triangle holding the origin plus the support above it, or reports no overlap when no
triangle holds it; an overlap within the convergence band is reported as no contact, since the shapes only touch.

A box or hull face manifold is the overlap polygon of the two faces: the incident face clipped to the reference
face's side planes and cut at the reference plane, on the incident face (so at its depth, which differs from the
reference face's only by a tilt), reduced to at most four corners, the deepest and the three that cover the most
area. A small face on a larger or turned one so keeps its whole footprint, and two faces crossing at an angle keep
the corners where their edges cross.

`collisions/SupportFaces.h` returns a box's or convex mesh's world-space planar faces (outward normal,
counter-clockwise corners, center; coplanar hull triangles merged in the hull's own space) with `faceToward` and
`clampToFace` queries. The contact response does not use them. `collisions::warmContactCaches` fills a static
collider's lazy caches (box world frame, hull face normals and support faces) before the islands read it in
parallel.

`queries/HullQueries` answers raycasts and sphere overlaps against a convex hull exactly, orienting each face away
from the hull center so a mirrored transform still works. The library operates on `ECS3DData` via accessors.

## Contact clipping

`sim/collisions/ContactManifold.h` builds contact points for two convex shapes from
plain vertex spans. Each shape's touching feature is the vertices within a tolerance of the extreme one along
the contact normal. The side with more points is the reference polygon; the other side is ordered into a
polygon, clipped to the reference's edge planes (Sutherland-Hodgman) and projected onto its plane. Features
with no area reduce to one point midway between their closest points.
A pair holding a convex mesh collider and a box or another hull takes its manifold from
`referenceFaceManifold` (`FaceManifold.h`), built the way the box pair is. Each shape offers its face normals
(`hullFaceNormals` for a hull, skipping slivers). The side whose face is more nearly parallel to the contact
normal is the reference, and alignments within `faceAlignmentTie` tie, going to the first collider. The box pair
uses the same band. The reference polygon is the convex hull of the touching vertices projected along the face. The
other side's touching face, or an edge of it, is clipped to the reference polygon's side planes
(`detail::clipToPolygon`, shared with the clipping above) and cut at the reference face's plane. A point within a
tolerance scaled to the coordinates' magnitude of a side plane counts as on it, so an edge lying on one is not split
by rounding. A corner, or an edge or face that clips to nothing, is clamped into the polygon instead. The overlap
polygon can have more than four corners, and then the deepest is kept with the three that cover the most area. The box
pair goes through the same function. So a box-shaped hull makes the same `Contact` a box does. A side with no faces
falls back to the clipping above.
A pair holding a capsule and a hull takes its manifold from that clipping in `Polytope`, at most
`Contact::points` wide, with the capsule's cap points as its feature.

## Inertia

`sim/InertiaTensor.h` computes each collider shape's world-space mass distribution: volume, centroid
and second moment per unit mass. It combines a body's colliders by volume share into one per-unit-mass inertia
tensor about the owner's position, with the parallel-axis theorem. `PhysicsSystem::worldInverseInertia` inverts it
for a body, so a sphere, capsule or hull spins and tips as its own shape does. A body with no usable collider falls
back to the box of its scale.

## Hull validation

`ConvexHullValidation.h` holds `isValidConvexHull`, which checks a hull's structure but
not its convexity: vertex and index counts within the limits, finite vertices, every index naming a vertex.
`makeUnitCubeHull` is the default hull for a collider.

## Terrain

The `Terrain` component (`data/objects/components/collisions/`) is a `Collider` that names terrain data by uuid and
never is a trigger. Its support is the box around the volume's occupied chunks, placed by the owner's position and
rotation but not its scale, since terrain space is metric. The volume is not serialized or packed: the app hands it
over with `setVolume`, and an edit to it calls `invalidateBounds`. It adds nothing to a body's inertia.
Scene queries hit it while its collision is enabled: `SceneQueries::raycast` casts in the terrain's local space with `terrain::raycast`, and
`overlapSphere` reads `Sampler::metricDistance` at the sphere center. The field saturates at the band, so for a sphere
whose radius is above 0.75 of the band and whose center reads the band, a ray along each of the six local axis
directions, as long as the radius, also counts as an overlap. That fallback is axis-only and can miss ground off
those axes. Layer masks and `ignoreObject` apply as for any collider.

**Edges.** A terrain with collision enabled and a bound volume is a static broad-phase edge, bounded by its
`getBoundingBox()` like any collider and never carrying a body, so it meets only dynamic bodies. Otherwise it stays
out of every pair and event (`isInert`). A terrain object must not carry a `RigidBody`: that is unsupported.
Collision ignores the body and treats the terrain as static, but `PhysicsSystem` would still integrate it.

**Sweep.** Only the dynamic side of a terrain pair asks. `probeTerrain` with the body's sweep answers both
`intersectsTerrain`, which sets that side's `lowerSees` or `higherSees` flag (no witness), and `findTerrainContacts`,
whose contacts set `approaching` when it does not intersect and are kept on the `SweepHit` for the solve. A box or hull
whose sweep is not traced is measured once for both answers. A trigger pair asks only `intersectsTerrain`. A terrain
never reaches `intersects`, `approaches` or `findContact`. Events come from those flags, so enter, stay and exit name
the terrain object's uuid. A trigger body gets events and no response, as against any collider.

**Solve.** `solveIsland` takes the contacts the sweep found (nothing moves a body between the two) and pushes one
`ContactPair` per contact: the body and its collider against the terrain with no other body, `manifold` the contact's
index. The solver
caches impulses under `PairKey` with that index as `subIndex` (zero for every other pair), so each wall of a gully keeps
its own warm start. Contacts are ordered by depth, so near-equal walls can swap ordinals between ticks: when the
exact sub-index is missing or faces another way, the lookup tries the pair's other sub-indices and takes the best
facing one. The normal and point re-validation still rejects a stale match.
`liftOutOfStatic` re-measures a terrain pair with `findTerrainContacts` and keeps the contact whose normal is closest to
the pair's own, dropping the pair when none is within 15 degrees.

**Threads.** Chunks are immutable and the `Sampler` is a stack local, so the narrow phase and the island solve read the
volume safely. The generator reads the terrain's `Transform` position and orientation, which are computed and cache
nothing, and its body's collider through `CapsuleCollider::getSegment`, `SphereCollider::getPosition` and `getRadius`,
which lazily rebuild the world shape and the transform pointer. The serial broad-phase `getBoundingBox()` of every edge
warms those first, since a bounding box recompute goes through the same `worldShape` and transform pointer. It does not
fill a box's `getWorldFrame` or a hull's `hullSupportFaces`, which the polytope path reads, so while a terrain is among
the edges the same serial loop calls `collisions::warmContactCaches` on every dynamic edge's collider. A hull's world
vertices are already current from the bounding box. There is no profile scope for terrain: its work runs inside the
narrow-phase and island regions, where scopes are not allowed.

**Not covered yet:** a sphere or capsule whose radius is above the band (see below), and a box or hull whose own
bounding sphere is larger than the band, which the sweep trace treats as touching from the start.

`collisions::findTerrainContacts` (`sim/collisions/TerrainContact.h`) is the contact generator for a collider
against a terrain; `CollisionSystem` calls it. It samples in terrain-local space with one
`terrain::Sampler` per call and returns early when every chunk the shape's swept bounds touch is absent or air.
A sphere or capsule is queried at its center or core points at most a voxel apart. Each one's surface is estimated from
the distance and gradient, then refined once at the estimate, giving a normal, a surface point and the shape's
distance from the ground. That distance is the larger of the tangent-plane distance (exact on a concave wall) and the
field's own distance at the center minus the radius (exact on a ridge or dome, where the tangent plane passes under
the shape); the raw field is used because `metricDistance` overshoots in a crease cell. A sphere also probes from
beside its center, because one sample blends the two walls of a gully into a single normal; a probe on the same wall
(within 15 degrees) as one already kept is dropped, since on a convex hilltop its tangent plane would overstate the
depth. When the center's cell reads a gradient shorter than 0.9 per voxel and the probes found two walls facing each
other, the blended center candidate is dropped in favor of those walls. Candidates within the contact margin, widened
by the part of the sweep that closes on the ground (a body moving away gets no speculative contact), are clustered by
normal (15 degrees, at most `maxTerrainManifolds`), deepest first, and each cluster becomes one `Contact`: a sphere
reports its deepest point, a capsule up to two spread apart. Contact points sit on the surface. The depths and
`advance` follow `findRoundApproach`: `overlap()` is the deepest point's depth, negative while apart, and `depth()` is
never zero.

A box or convex hull takes the polytope path (`collisions/TerrainPolytopeContact.cpp`, sharing the terrain frame,
surface refinement, clustering and contact builder with the above through `TerrainContactInternal.h`). Its early-out
uses the collider's bounding box, widened by the sweep, the margin and a voxel, and also returns when every sample that
box reads is air (`TerrainVolume::isAirWithin`, answered per 8-sample brick) and the band's edge is beyond the furthest
a point is measured from. Query points have radius zero: every
vertex, then points along each edge and over each face (`hullSupportFaces` polygons for a hull, six rectangles for a box)
no more than a voxel apart, with face points on cell centers kept when inside the polygon. They are built on every call
from the collider's world caches, not cached on the collider, since a lazy cache would be written by several threads in
the narrow phase. The total is capped at `maxTerrainQueryPoints` (512) with the vertices always kept: when the
full-density points would pass it, edges and faces are ranked by the raw field distance at their middle, nearest
first, and each in turn takes the finest spacing (steps
of 25 percent over a voxel) that leaves room for the groups after it, so a face lying on the ground keeps its full grid
while it fits. A point whose raw distance is beyond the margin and the sweep's length (`furthestReach`) is skipped
before its surface is looked for: a candidate is never nearer the ground than its point's raw reading, so it could not
reach. So is one with no usable gradient. The rest are measured like a sphere of radius zero and clustered by normal as above; a
cluster is reduced with `detail::spreadOut` on quantized depths to at most four points, so a box lying flat reports its
four bottom corners as one contact. The points are the collider's own surface points, as `referenceFaceManifold`
reports for a box on a static box, and their depths are positive inside the ground and negative while short of it.

**Fast bodies.** Past the band the field is flat and has no gradient, so a body whose sweep is longer than a voxel
could start in saturated air and end past or inside the ground with no contact reported. `reachAlongSweep` sphere
traces first: a sphere or capsule traces each core point with its radius, a box or hull its bounding sphere (the
center of its bounds, the distance to its furthest vertex). Each step is the clearance (the smaller of the metric and
raw distance, minus the radius) less half the contact margin, at least a tenth of a voxel, capped at 256 steps, until
the clearance is within `contactMargin` or the sweep is used up; a sweep of a voxel or less is not traced, and a trace
that reaches nothing leaves the query as it was. The contacts are then made as if the collider stood at that fraction
`t` of the sweep, with the rest of the sweep, and `shiftTracedContact` re-expresses them where the collider really is:
`depth()` is unchanged, `advance` grows by `t` times the sweep's closing speed along the normal and every point depth
shrinks by the same, so `overlap()` is the whole gap and the solver lets the body travel exactly to the surface. The
points move back by the distance travelled, which keeps their arms about the collider's center as they were where it
touched (left at the touch, a sphere's far-off point would give the solver a long lever arm). With a sweep longer than a
voxel, each cluster is trimmed to the members within half a voxel of its nearest, since every vertex of a box then
reaches the ground but only the leading ones are about to touch, and a manifold that took a trailing one would turn the
body. A box or hull whose bounding sphere is wider than half the band sees no ground through it, so each of its vertices
is traced as a point and the earliest taken. If the step cap runs out the fraction reached is used rather than none.

**Deep penetration.** A query point whose raw distance is at or below -3.9 voxels reads saturated inside the ground,
with no gradient. `findDeepSurface` steps it back a voxel at a time (at most 64) against the sweep, or up in terrain
space when there is none, until the field leaves saturation, then finds the surface there. The contact is measured
from the original point against that surface, so its depth is the stepped distance plus the depth where the band was
found, with the raw field taken as unknown. A step that jumps from saturated inside to a reading at or above zero
(which an absent chunk gives, as +4) bisects between the last inside point and that one for the zero crossing and takes
the field gradient there as the normal, or the step direction when the gradient is unusable, so where the steps land on
the lattice does not change the answer: a point deep in a column that reaches the volume's edge takes the edge as the
way out. Sphere, capsule, box and hull points all take it, and `intersectsTerrain` sees it. `liftOutOfStatic` then
lifts the body, and its re-measure finds the contact again because the lift ends within half a margin of the surface.

The shape's radius must stay below the distance band, about 3.5 voxels: a larger one reads a saturated center with no
gradient and loses its contact, and larger shapes are later work. The collider's world shape caches lazily, so a caller
on several threads warms the collider first; the terrain side only reads. The early-out walks only as many chunks as the
box or the volume holds, whichever is fewer.

`TerrainRestingTest.cpp` and `TerrainGateTest.cpp` are the quality gate for the contact generator: scenes at the server
tick on a 2.5 unit voxel, run with the solver and generator as they are, that print what they measure. It covers a box,
hull, capsule and sphere on a 20 degree slope (a sphere or a capsule lying across it rolls on a box ramp too, so its
gate is the same roll as on the ramp), five-box stacks and boxes over 20 voxels wide on rolling terrain, a ridge and a
gully, each both on and off the voxel lattice, downward raycasts against the sampled surface and the analytic height, a
brush dig and fill under a resting box, a hull dropped into a dimple (it topples onto an edge and rests), and the cost
of 200 resting boxes against the same boxes on a box floor. A flat box's four corners at equal depth are in
`TerrainContactTest.cpp`.

The `DISABLED_` scenes are the ones the generator does not hold, each with its numbers in a comment. The stack on a dome
crest at a cell corner drifts about 0.5 units in 60 s, because the one-sided trilinear gradient there tilts the crest
normal about 0.95 degrees; the same stack at a mid-cell crest holds, and that is an enabled test. The trough and flank
stacks and the balanced wide box on a ridge also fail when moved 0.37 voxel off the lattice (drifts of 0.03 to 0.05
units, and the ridge box sinks 0.2 units into the analytic ridge). The lattice-aligned flank stack holds on MSVC Release
but drifts 0.026 units in the macOS clang Debug build, so it is disabled too. The collision pass for the 200 resting
boxes measured about 1.9 times the box floor's (Release, MSVC, simulation library optimized and test sources not),
against a target of 1.25. The cost test runs only in Release builds.

## Convex mesh collider

The `ConvexMeshCollider` component (`data/objects/components/collisions/`) holds its
hull inline, as local-space vertices and triangle indices. The server therefore works without the model the
hull came from. Its `sourceModel` field only records where the hull was built. A fresh one is the unit cube
and places itself like a `BoxCollider` with the same offsets. Its `setHull`, `loadFromJSON` and `unpack`
refuse an invalid hull and change nothing. Before it allocates, `unpack` bounds each count. It caches its world
mass distribution, filled by `InertiaTensor`'s `hullOf` and keyed on a counter its world vertices bump when
they rebuild. The narrow phase, scene queries, scripts and the inspector all handle it, as their own sections
describe.
