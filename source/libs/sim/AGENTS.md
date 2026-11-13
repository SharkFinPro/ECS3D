# AGENTS.md - ECS3DSim

Agents load this file when they read a file in `source/libs/sim/`. It holds what a change here must keep. Read
[docs/architecture/physics.md](../../../docs/architecture/physics.md) before changing collision response, friction,
resting or the contact manifold: that behavior is tuned as a whole, and the suite pins much of it.

- Linear velocity is displacement per tick. Angular velocity is a world-space axis in degrees per second. Convert
  angular velocity to radians per tick before combining the two; `applyVelocityChange`, `applyImpulse` and
  `CollisionSystem::fixedUpdate` take `dt` for that.
- A tick moves each body by its velocity, then applies forces and gravity, then finds and solves the contacts.
  `ContactSolver` owns the response: it changes velocities and pushes bodies out of overlap, and nothing else may.
- A body's gravity is in meters per second squared, applied as `PhysicsSystem::unitsPerMeter` world units per meter,
  so launch speeds and heights in content are in world units.
- The inertia tensor is per unit mass and comes from the body's collider shapes (`InertiaTensor.h`). A velocity change
  moves and turns a body alike whatever its mass, while an impulse is divided by the mass.
- Orientation turns by composing a rotation onto it, not by adding to the Euler angles, and the result is written back
  through `Transform::setWorldRotation`.
- Scripts reach physics only through data: forces queue on the `RigidBody` with a `ForceMode`, and collision events
  cross to scripts as plain uuid pairs that `ServerApp` carries. This library does not link scripting.
- `CollisionSystem` hands each island's contacts to the solver lowest first by bounding-box minY, so a stack settles
  the same wherever along x it sits. Islands solve in parallel, so a solve may write only its own island's bodies. A static collider it
  reads is shared between islands: a new lazy cache on a collider must also be filled by
  `collisions::warmContactCaches`.
- The `CollisionSystem` profile scopes (`Broad phase`, `Narrow phase`, `Response`) open on the calling thread around
  each phase; never put one inside the OpenMP region.
- The narrow phase entry points are `collisions::findContact` and `collisions::intersects` (`collisions/NarrowPhase.h`).
- Compare physics results in tests with a tolerance: results pass through matrix math and accumulation.
