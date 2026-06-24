#include "ScriptSystem.h"

ScriptSystem::ScriptSystem(std::shared_ptr<ManagedHost> host)
  : m_host(std::move(host))
{}

void ScriptSystem::start(const std::shared_ptr<SceneAsset>& scene)
{
  // TODO: attach + start the managed script instances for every Script component in the scene.
  (void)scene;
}

void ScriptSystem::stop(const std::shared_ptr<SceneAsset>& scene)
{
  // TODO: stop + detach the managed script instances.
  (void)scene;
}

void ScriptSystem::fixedUpdate(const std::shared_ptr<SceneAsset>& scene, const float dt)
{
  // TODO: invoke each script's managed fixedUpdate via ManagedHost. Bindings (applyForce, move,
  // TODO:   keyIsPressed, ...) read/write the ECS3DData component data on the server.
  (void)scene;
  (void)dt;
}
