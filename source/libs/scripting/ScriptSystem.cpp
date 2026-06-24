#include "ScriptSystem.h"
#include "bindings/BindingContext.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/Script.h>

ScriptSystem::ScriptSystem(std::shared_ptr<ManagedHost> host)
  : m_host(std::move(host))
{}

void ScriptSystem::start(ObjectManager& objectManager)
{
  // TODO: attach + start the managed script instance for every Script component (via the migrated
  // TODO:   ScriptManager), writing each Script's field blob into the live C# instance.
  for (const auto& object : objectManager.getAllObjects())
  {
    for (const auto& script : object->getScripts())
    {
      (void)std::dynamic_pointer_cast<Script>(script);
    }
  }
}

void ScriptSystem::stop(ObjectManager& objectManager)
{
  // TODO: stop + detach the managed script instances.
  (void)objectManager;
}

void ScriptSystem::fixedUpdate(ObjectManager& objectManager, const float dt)
{
  // Point the C-ABI bindings at the scene so script callbacks (applyForce, move, ...) can resolve a
  // uuid to its object while they run.
  BindingContext::setObjectManager(&objectManager);

  for (const auto& object : objectManager.getAllObjects())
  {
    for (const auto& scriptComponent : object->getScripts())
    {
      const auto script = std::dynamic_pointer_cast<Script>(scriptComponent);
      if (!script)
      {
        continue;
      }

      // TODO: attach (if needed) + invoke the managed fixedUpdate via ManagedHost/ScriptManager:
      // TODO:   m_scriptManager->fixedUpdate(object->getUUID(), script->getClassName().c_str(), dt).
      (void)script;
    }
  }

  (void)dt;
}
