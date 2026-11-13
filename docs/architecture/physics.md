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

## Convex mesh collider

The `ConvexMeshCollider` component (`data/objects/components/collisions/`) holds its
hull inline, as local-space vertices and triangle indices. The server therefore works without the model the
hull came from. Its `sourceModel` field only records where the hull was built. A fresh one is the unit cube
and places itself like a `BoxCollider` with the same offsets. Its `setHull`, `loadFromJSON` and `unpack`
refuse an invalid hull and change nothing. Before it allocates, `unpack` bounds each count. It caches its world
mass distribution, filled by `InertiaTensor`'s `hullOf` and keyed on a counter its world vertices bump when
they rebuild. The narrow phase, scene queries, scripts and the inspector all handle it, as their own sections
describe.
