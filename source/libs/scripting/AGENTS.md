# AGENTS.md - ECS3DScripting

Agents load this file when they read a file in `source/libs/scripting/`. It holds what a change here must keep. The
full notes are in [docs/architecture/scripting.md](../../../docs/architecture/scripting.md) and
[docs/architecture/script-bindings.md](../../../docs/architecture/script-bindings.md).

- `ScriptSystem` drives scripts through the `ScriptRuntime` interface and does not include the CLR host, so the test
  suite compiles `ScriptSystem.cpp` and tests it against a fake. `ScriptEngine` is the CLR-backed implementation,
  built by `ScriptEngineFactory`.
- Each native binding is a function-pointer struct mirrored by a C# `[StructLayout(Sequential)]` struct and
  registered through `Bridge`. **Add new fields at the end of both**, or the layouts stop matching.
- `bindings/` covers Transform, RigidBody, InputUtils, World, Camera, Collider, ModelRenderer, LightRenderer and
  PlayerController, plus `InputState` and `BindingContext`. `bindings/BindingCoverage.h` fails the build for a
  `ComponentType` with no scripting decision.
- Reaching another object's component is a `tryGet` that returns false when it is absent; nothing throws in the tick
  loop.
- A binding that changes scene structure buffers the change on `BindingContext`, and `ServerApp` drains and
  replicates it after the tick. While a `ScriptPassGuard` is alive, `ObjectManager::addObject` queues a new object
  instead of growing the vector a script pass iterates.
- Scripts reach `sim` only through data: forces queue on the `RigidBody`, queries arrive as function pointers
  `ServerApp` injects into `BindingContext`, and collision events arrive as plain uuid pairs. This library does not
  link `sim`.
- On the managed side, `ScriptBase` (object scripts) and `SceneScript` (scene scripts) share `ScriptInstance`.
  `ScriptBridge/` builds through CMake only, and its tests live in `source/tests/managed/`.
