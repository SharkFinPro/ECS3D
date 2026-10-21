#ifndef PHYSICSPLAYGROUNDTESTHELPERS_H
#define PHYSICSPLAYGROUNDTESTHELPERS_H

#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "TemplateTestHelpers.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Shared by the Physics Playground suites: the project JSON as a flat object list, and a headless tick loop.
namespace playgroundtest {
  inline const std::filesystem::path sampleFolder = templateTest::sourceRoot / "templates" / "physics-playground";
  constexpr float dt = 1.0f / 50.0f;

  inline const nlohmann::json& project()
  {
    static const nlohmann::json loaded = templateTest::projectJson(sampleFolder);
    return loaded;
  }

  inline void collect(const nlohmann::json& object, std::vector<const nlohmann::json*>& into)
  {
    into.push_back(&object);
    for (const auto& child : object.at("children"))
    {
      collect(child, into);
    }
  }

  inline std::vector<const nlohmann::json*> allObjects()
  {
    std::vector<const nlohmann::json*> objects;
    for (const auto& object : project().at("assets").at("scenes").at(0).at("objects"))
    {
      collect(object, objects);
    }

    return objects;
  }

  inline std::vector<const nlohmann::json*> named(const std::string& prefix)
  {
    std::vector<const nlohmann::json*> matches;
    for (const auto* object : allObjects())
    {
      if (object->at("name").get<std::string>().rfind(prefix, 0) == 0)
      {
        matches.push_back(object);
      }
    }

    return matches;
  }

  inline const nlohmann::json& only(const std::string& name)
  {
    static const nlohmann::json none = nlohmann::json::object();
    for (const auto* object : allObjects())
    {
      if (object->at("name") == name)
      {
        return *object;
      }
    }

    ADD_FAILURE() << "no object named " << name;
    return none;
  }

  inline nlohmann::json scriptField(const nlohmann::json& object, const std::string& className,
                                    const std::string& field)
  {
    for (const auto& script : object.at("scripts"))
    {
      if (script.at("className") != className)
      {
        continue;
      }

      for (const auto& entry : script.at("fields"))
      {
        if (entry.at("name") == field)
        {
          return entry.at("value");
        }
      }
    }

    ADD_FAILURE() << object.at("name") << " has no " << className << "." << field;
    return nlohmann::json();
  }

  inline std::map<std::string, nlohmann::json> prefabsByUuid()
  {
    std::map<std::string, nlohmann::json> prefabs;
    for (const auto& prefab : project().at("assets").at("prefabs"))
    {
      prefabs[prefab.at("uuid").get<std::string>()] = prefab;
    }

    return prefabs;
  }

  inline std::string prefabUuid(const std::string& name)
  {
    for (const auto& prefab : project().at("assets").at("prefabs"))
    {
      if (prefab.at("name") == name)
      {
        return prefab.at("uuid").get<std::string>();
      }
    }

    ADD_FAILURE() << "no prefab named " << name;
    return {};
  }

  // A station child's world position: station roots are only translated.
  inline glm::vec3 worldPositionOf(const nlohmann::json& child)
  {
    for (const auto* station : named("Station: "))
    {
      for (const auto& candidate : station->at("children"))
      {
        if (&candidate == &child)
        {
          return templateTest::positionOfJson(*station) + templateTest::positionOfJson(child);
        }
      }
    }

    return templateTest::positionOfJson(child);
  }

  inline std::shared_ptr<Object> objectNamedExactly(const ObjectManager& objectManager, const std::string& name)
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

  inline float tiltDegrees(const std::shared_ptr<Object>& object)
  {
    const auto up = fixtures::transformOf(object)->getOrientation() * glm::vec3(0, 1, 0);
    return glm::degrees(std::acos(std::clamp(up.y, -1.0f, 1.0f)));
  }

  inline void tick(const ObjectManager& objectManager, const int count)
  {
    CollisionSystem collisionSystem;
    for (int i = 0; i < count; ++i)
    {
      PhysicsSystem::fixedUpdate(objectManager, dt);
      collisionSystem.fixedUpdate(objectManager, dt);
    }
  }
}

#endif //PHYSICSPLAYGROUNDTESTHELPERS_H
