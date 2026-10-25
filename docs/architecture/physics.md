# Physics and collision

The root `AGENTS.md` summarizes this topic and links here. Read this file before changing the code it describes,
and update it in the same change when that behavior moves.

## ECS3DSim

`ECS3DSim` — `PhysicsSystem` (integration, forces, response; linear velocity is displacement per tick while
angular velocity is a world-space axis in degrees per second, so convert angular velocity to radians per tick
before combining the two - `applyVelocityChange`, `applyImpulse`, `handleCollision` and
`CollisionSystem::fixedUpdate` take `dt` for that; the inertia tensor is per unit mass and comes from the body's
collider shapes in world geometry (`InertiaTensor.h`: a solid sphere, box, capsule - a cylinder and two
hemispheres - or a hull summed from tetrahedra to its triangles; a compound body weights each collider by its
volume about the owner's position, a trigger adds nothing, and a body with no collider is a solid box whose
half-extents are its scale), so a velocity change moves and turns a body alike whatever its mass while an
impulse is divided by the mass for both; it turns the orientation by composing a rotation onto it rather than by
adding to the Euler angles, and writes the result back through `Transform::setWorldRotation` since the rotation
it turned is combined with the parent's, and an off-center impulse reaches it through the inertia tensor taken
in world axes - a box manifold is resolved with one impulse at `supportPoint`, the center of mass clamped into
the manifold, sized to stop that point's own velocity, spin included, through its effective mass, and leaving a
body already moving away from its support as it is - a dynamic pair sizes the impulse against both bodies'
inverse mass and inertia and trades the closing speed elastically (below `minBounceSpeed`, two units a second,
it does not bounce), and splits the separation by inverse mass too, but a body that rested on a support last
tick hands the part of any push from another body that points into that support to it, so a stack holds whatever
its masses and what lands on it stops as on static geometry, without a bounce - and its support's friction holds
the part along the support too, inside the friction cone and up to what that friction can take
(`PhysicsSystem::squeeze`), so a light body is squeezed out from under a heavy one only by the excess, pushed
through its center; "into" and "along" are taken against the support's contact normal
(`RigidBody::getSupportNormal`, the flattest upward contact normal of the previous tick, world up when none), so
a stack on a slope holds as on flat ground, and the load a body passes to its support is taken along that normal
too, and a vertical speed under `minRiseSpeed` does not count as a body rising; a contact between two resting
bodies is resolved only from the upper one, since resolving it again from below leaves the pair touching
exactly, which the sweep misses, but one the resting body below does resolve, because the body landing on it had
no contact in its own pass, has its friction and passed-on load resolved with the roles swapped, so it acts as
it would from above, the push landing at the landing body's own center of mass clamped into the manifold; then
Coulomb friction opposes the contact point's sliding up to the coefficient (the geometric mean of two bodies',
the body's own against static geometry) times the impulse pressing the pair together, which includes the load
stacked on the body (`RigidBody::getStackedLoad`, reset each tick), acting through the center on a face under
the center of mass and at the point otherwise, and a resting body's support takes a drag from above first, up to
what its own friction holds, leaving the body unturned and recording the impulse along the support it held
(`RigidBody::getHeldImpulse`, reset each tick, a vector so pushes the opposite way cancel) so the body's own
contact with that support has only the rest; a face contact also resists turning about its normal, up to
friction times load times two thirds of the manifold's mean radius, and a sphere's contact resists rolling at a
tenth of that friction, since neither slides at the support point (rolling resistance and the resting-face and
flush logic both use the collider `CollisionSystem` hands `handleCollision`, not the body owner's, since on a
compound body it belongs to a child; both take the faces of that collider and of the support from
`collisions::supportFacesOf`, so a box and a convex mesh rest alike; a face with over four corners is reduced to
its convex outline (`collisions/PlanarHull.h`) before the center-of-mass test; a sphere is not laid flush; a
capsule lying in a line contact under its center of mass is turned into its support's face plane; a body whose
resting face meets a neighbor within 30 degrees (a finely faceted hull, like a drum) rolls on rather than
landing on a facet, where a box lands flat; and `layFlush` turns the owner's world orientation by the angle
between the two faces); a body that friction fully holds on a slope is moved back by the slide its gravity and
queued forces caused (`holdAgainstDrive`, not for spheres or capsules, which roll), so it stays put below the
friction angle; nothing else damps linear velocity, so a body in flight keeps its horizontal speed; before the
impulse, each contact point sheds, inertia-weighted, only the spin driving it into its support, repeated over
the points until none sheds more, except that a body which rested on that plane last tick (its recorded support
normal agrees), with its center of mass over the face, keeps spin that turns it past the flush window in one
tick, so a push can tip it over its far edge instead of sliding it upright; a landing or finely faceted body
still sheds it, and a contact whose normal is within a degree of level counts as a side contact, whichever way
rounding tilts it, and a body resting on something below it has leftover spin under `restAngularSpeed` dropped,
so it comes exactly to rest; a body whose center of mass is over a face it rests on, within the degree or so the
manifold already counts as flush, is turned flush with the support's face, since nothing torques it the rest of
the way down; a box on an edge or corner whose spin would carry its face flat against a box support within the
tick, with its center of mass over that face, lands on the face that tick instead, since one tick of edge
support turns a box a degree or more and it would otherwise rock from edge to edge). A sphere or capsule rolls
with its own world radius as the rolling arm, whatever its offset from the body's owner. Lying along a line
contact it rolls only about its own axis, and a body resting on a capsule is slowed by that rolling resistance
too. A capsule lying on a flat support touches along a line, so its center of mass between the two contact
points counts as supported. Friction there rolls it only by the part across the line; the part along acts
through the center, since the line's far end holds the capsule against pitching. It grinds against turning about
the normal with the mean lever arm of a load spread evenly along the line. Take a and b as the distances from
the center of mass to the two ends, and that arm is (a^2 + b^2) / (2 (a + b)). `CollisionSystem`
(sweep-and-prune in minX order - each edge scans forward only, testing a pair once from its lower-indexed edge
when either side is dynamic and handing the hit to every dynamic side, with each edge's hits kept in ascending
edge order - but responses resolve top-down by bounding-box minY so a stack settles the same whichever side of
it the load sits), calling the narrow phase under `collisions/` — `NarrowPhase.h`'s `findContact`/`intersects`
are its entry points. A pair of spheres and capsules skips GJK, since a sphere is a capsule with a zero-length
core segment. That pair is solved from the closest points between the two core segments, treated as two spheres.
Parallel overlapping capsules report two manifold points. Every other pair goes through GJK/EPA, where a
capsule's contact feature is its cap points facing the other shape, clamped onto a box's footprint. EPA stops
once the support point lies on the closest face's plane, and treats a face as visible only beyond a tolerance
scaled by magnitude, since rounding would otherwise delete coplanar faces and leave a hole. The same tolerance
decides whether a new face looks inward, since a vertex on its plane is noise. A rebuild that cannot close its
horizon is undone and ends EPA. `collisions/SupportFaces.h` returns a box's or convex mesh's world-space planar
faces (outward normal, counter-clockwise corners, center; coplanar hull triangles merged in the hull's own
space) with `faceToward` and `clampToFace` queries; PhysicsSystem's resting logic uses it, and
`supportFaceToward` builds the single face a box needs. A pair whose colliders resolve to the same rigid body,
or where one object is an ancestor of the other, is skipped before the narrow phase. A box or hull face manifold
also adds the reference face's corners that lie over the incident face, at the incident face's depth, and
spreads the result to at most four points. A small face on a larger or turned one so keeps its whole footprint
instead of two diagonal corners. A contact holds a resting body like a stacked load (`pressesIntoSupport`) only
within 45 degrees of that body's support normal. A flatter push, such as a leaning domino, can tip it.
`findSupport` decides whether the center of mass is over the support by dropping it straight down along world y,
not along the contact normal. On a slope a body past its downhill edge then tips or rolls. Operates on
`ECS3DData` via accessors. OpenMP if available. `queries/HullQueries` answers raycasts and sphere overlaps
against a convex hull exactly, orienting each face away from the hull center so a mirrored transform still
works.

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
uses the same band. The reference polygon is the convex hull of the touching vertices projected along the face.
The other side's touching vertices are clamped into it along the face plane, keeping their depth. So a
box-shaped hull makes the same `Contact` a box does. A side with no faces falls back to the clipping above.
A pair holding a capsule and a hull takes its manifold from that clipping in `Polytope`, at most
`Contact::points` wide, with the capsule's cap points as its feature. `PhysicsSystem` skips the box-axis flush
snap when a hull is in the pair, since a hull has no box faces to lie flush against.

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
