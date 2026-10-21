#ifndef DEFAULTWELCOMETESTHELPERS_H
#define DEFAULTWELCOMETESTHELPERS_H

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "ProjectSerializer.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "scenes/SceneManager.h"

#include <nlohmann/json.hpp>
#include <memory>
#include <stdexcept>
#include <string>

// Shared by the Welcome scene suites: loading the built-in project and finding things in it.
namespace welcometest {
struct LoadedProject
{
  std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
  AssetRegistry assetRegistry;
  SceneManager sceneManager;
};

inline void load(LoadedProject& loaded, const nlohmann::json& project)
{
  registerDataComponents(*loaded.componentRegistry);
  const ProjectSerializer serializer(&loaded.assetRegistry, &loaded.sceneManager, loaded.componentRegistry);
  serializer.deserialize(project);
}

inline const nlohmann::json& sceneNamed(const nlohmann::json& project, const std::string& name)
{
  for (const auto& scene : project.at("assets").at("scenes"))
  {
    if (scene.at("name") == name)
    {
      return scene;
    }
  }

  throw std::runtime_error("no scene named " + name);
}

inline std::shared_ptr<Object> objectNamed(const ObjectManager& objectManager, const std::string& name)
{
  for (const auto& object : objectManager.getAllObjects())
  {
    if (object->getName() == name)
    {
      return object;
    }
  }

  return nullptr;
}
}

#endif //DEFAULTWELCOMETESTHELPERS_H
