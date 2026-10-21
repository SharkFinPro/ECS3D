#ifndef TEMPLATETESTHELPERS_H
#define TEMPLATETESTHELPERS_H

#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "ProjectSerializer.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RotationConvention.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

// JSON-level helpers for the suites that check a shipped template folder.
namespace templateTest {
  inline const std::filesystem::path sourceRoot = std::filesystem::path(ECS3D_SOURCE_DIR) / "source";

  inline glm::vec3 vec3Of(const nlohmann::json& value)
  {
    return { value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>() };
  }

  inline bool hasComponent(const nlohmann::json& object, const std::string& type)
  {
    return std::ranges::any_of(object.at("components"),
                               [&](const nlohmann::json& component) { return component.at("type") == type; });
  }

  inline nlohmann::json componentOf(const nlohmann::json& object, const std::string& type,
                                    const std::string& subType = "")
  {
    for (const auto& component : object.at("components"))
    {
      if (component.at("type") == type && (subType.empty() || component.at("subType") == subType))
      {
        return component;
      }
    }

    ADD_FAILURE() << object.at("name") << " has no " << type;
    return nlohmann::json::object();
  }

  inline glm::vec3 positionOfJson(const nlohmann::json& object)
  {
    return vec3Of(componentOf(object, "Transform").at("position"));
  }

  inline glm::vec3 scaleOfJson(const nlohmann::json& object)
  {
    return vec3Of(componentOf(object, "Transform").at("scale"));
  }

  // The objects of a scene, serialized, in file order.
  inline std::vector<nlohmann::json> sceneObjects(const SceneAsset& scene)
  {
    std::vector<nlohmann::json> objects;
    for (const auto& object : scene.getObjectManager()->getObjects())
    {
      objects.push_back(object->serialize());
    }

    return objects;
  }

  inline nlohmann::json objectNamed(const SceneAsset& scene, const std::string& name)
  {
    for (const auto& object : sceneObjects(scene))
    {
      if (object.at("name") == name)
      {
        return object;
      }
    }

    ADD_FAILURE() << "no object named " << name;
    return nlohmann::json::object();
  }

  inline std::vector<nlohmann::json> objectsStartingWith(const SceneAsset& scene, const std::string& prefix)
  {
    std::vector<nlohmann::json> matches;
    for (const auto& object : sceneObjects(scene))
    {
      if (object.at("name").get<std::string>().rfind(prefix, 0) == 0)
      {
        matches.push_back(object);
      }
    }

    return matches;
  }

  struct LoadedTemplate
  {
    std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
    AssetRegistry assetRegistry;
    SceneManager sceneManager;
    bool loaded = false;

    explicit LoadedTemplate(const std::filesystem::path& folder)
    {
      registerDataComponents(*componentRegistry);
      const ProjectSerializer serializer(&assetRegistry, &sceneManager, componentRegistry);
      loaded = serializer.load((folder / "project.ecs3d").string());
    }
  };

  inline nlohmann::json projectJson(const std::filesystem::path& folder)
  {
    std::ifstream file(folder / "project.ecs3d");
    EXPECT_TRUE(file.is_open());
    return nlohmann::json::parse(file);
  }

  inline std::string readText(const std::filesystem::path& path)
  {
    std::ifstream file(path);
    EXPECT_TRUE(file.is_open()) << path.string();
    return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
  }

  // Every model, texture and collider source model an object and its children name.
  inline void collectReferences(const nlohmann::json& object, std::vector<std::string>& references)
  {
    for (const auto& component : object.at("components"))
    {
      if (component.at("type") == "ModelRenderer")
      {
        for (const char* key : { "modelUUID", "textureUUID", "specularMapUUID" })
        {
          references.push_back(component.at(key).get<std::string>());
        }
      }
      else if (component.at("type") == "Collider" && component.contains("sourceModel"))
      {
        references.push_back(component.at("sourceModel").get<std::string>());
      }
    }

    for (const auto& child : object.at("children"))
    {
      collectReferences(child, references);
    }
  }

  // The collider's hull vertices in world space, through the object's Transform.
  inline std::vector<glm::vec3> worldHullVertices(const nlohmann::json& object)
  {
    const auto transform = componentOf(object, "Transform");
    const auto position = vec3Of(transform.at("position"));
    const auto scale = vec3Of(transform.at("scale"));
    const auto orientation = eulerDegreesToQuat(vec3Of(transform.at("rotation")));

    const auto flat = componentOf(object, "Collider", "ConvexMesh").at("vertices");

    std::vector<glm::vec3> vertices;
    for (std::size_t i = 0; i < flat.size(); i += 3)
    {
      const glm::vec3 local(flat[i].get<float>(), flat[i + 1].get<float>(), flat[i + 2].get<float>());
      vertices.push_back(position + orientation * (scale * local));
    }

    return vertices;
  }
}

#endif //TEMPLATETESTHELPERS_H
