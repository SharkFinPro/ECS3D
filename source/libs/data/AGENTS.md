# AGENTS.md - ECS3DData

Agents load this file when they read a file in `source/libs/data/`. It holds what a change here must keep. The full
notes are in the ECS3DData section of [docs/architecture/library-reference.md](../../../docs/architecture/library-reference.md).

Related design notes: [replication.md](../../../docs/architecture/replication.md),
[prefabs.md](../../../docs/architecture/prefabs.md), [undo-redo.md](../../../docs/architecture/undo-redo.md),
[physics.md](../../../docs/architecture/physics.md).

- The headless server links this library, so it takes no Vulkan or ImGui include. Lifting that needs a deliberate
  architecture decision recorded in the root `AGENTS.md`.
- A component holds state only: fields plus `serialize`/`loadFromJSON` for files and `pack`/`unpack` for the wire.
  Behavior lives in a system outside this library. Thread a new field through all four, and register a new component
  in `registerDataComponents()`.
- `ComponentType`'s value is the wire discriminator, assigned by enumerator order. A new enumerator also needs a row in
  the scripting library's `bindings/BindingCoverage.h`, or the build fails.
- Float and vec3 setters ignore non-finite input, since a non-finite float saves as json null and makes the file
  unreadable. New setters follow suit.
- A component that refuses a payload throws `RefusedPayload` (`Component.h`) before it assigns anything.
- `Transform`'s world getters compose `parentWorld * local`, with the Euler mapping in `RotationConvention.h` that every
  system shares. Collider caches key on `Transform::getWorldUpdateID`.
- Recursion into a wire or JSON object tree stops at `maxObjectDepth` (`objects/Object.h`).
- A user preference belongs in `ECS3DSettings`, not in a scene, a prefab or a snapshot.
