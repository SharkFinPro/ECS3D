# AGENTS.md - ECS3DTests

Agents load this file when they read a file in `source/tests/`. It holds what a change here must keep. The full notes
are in [docs/architecture/testing.md](../../docs/architecture/testing.md).

- The suite stays headless: no window, GPU or server. It links `ECS3DData`, `ECS3DSim`, `ECS3DSettings` and
  `ECS3DNetProtocol`. A few sources from other libraries compile straight into it because they need no CLR, Vulkan or
  ImGui; `CMakeLists.txt` explains when another one may join them.
- Add each new test file to the `add_executable` source list in `CMakeLists.txt`. A file left out compiles nothing
  and adds no coverage.
- Build a scene through `fixtures::Scene` and the helpers in `TestScene.h` instead of repeating the setup.
- Compare a `glm::vec3` that came out of matrix math with the tolerance helpers, not exactly.
- `Object::serialize` fills `"components"` from an unordered map. Sort the components before comparing whole json.
- Give an assertion that only checks absence a positive control in the same test, so a subsystem that stopped working
  cannot pass it.
- Managed tests live in `managed/` and have their own `AGENTS.md`.
