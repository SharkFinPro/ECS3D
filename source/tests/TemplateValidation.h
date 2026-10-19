#ifndef TEMPLATEVALIDATION_H
#define TEMPLATEVALIDATION_H

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "ProjectSerializer.h"
#include "assets/AssetRegistry.h"
#include "objects/ObjectManager.h"
#include "scenes/SceneManager.h"
#include "templates/TemplateManifest.h"

#include <nlohmann/json.hpp>
#include <uuid.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

// Checks a template folder against the engine's built-in assets and scripts. Each problem starts with a
// class tag ("manifest:", "project:", "entryScene:", "thumbnail:", "asset-file:", "reference:",
// "script-class:", "script-uuid:", "prefab:") so a test can assert which kind of rot it caught.
namespace templateValidation {
  struct Context {
    std::filesystem::path builtInAssets;
    std::filesystem::path builtInScripts;
  };

  inline std::string canonical(const std::string& text)
  {
    if (const auto parsed = uuids::uuid::from_string(text))
    {
      return uuids::to_string(*parsed);
    }

    return text;
  }

  inline bool isUUID(const std::string& text)
  {
    return uuids::uuid::from_string(text).has_value();
  }

  // Matched by declaration, not file name, since one file may hold several classes.
  inline std::set<std::string> scriptClassesIn(const std::filesystem::path& root)
  {
    static const std::regex declaration(R"(class\s+(\w+)[^{;]*:\s*(ScriptBase|SceneScript)\b)");
    std::set<std::string> classes;
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec))
    {
      return classes;
    }

    for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
    {
      if (!it->is_regular_file() || it->path().extension() != ".cs")
      {
        continue;
      }

      std::ifstream file(it->path());
      std::stringstream text;
      text << file.rdbuf();
      const auto content = text.str();
      for (std::sregex_iterator match(content.begin(), content.end(), declaration), none; match != none; ++match)
      {
        classes.insert((*match)[1].str());
      }
    }

    return classes;
  }

  inline bool staysInside(const std::string& relative)
  {
    const std::filesystem::path path(relative);
    if (relative.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())
    {
      return false;
    }

    const auto normal = path.lexically_normal();
    return !normal.empty() && *normal.begin() != "..";
  }

  struct Walk {
    std::set<std::string> objectUUIDs;
    std::vector<const nlohmann::json*> scriptEntries;
    std::vector<const nlohmann::json*> components;
  };

  inline void collectObject(const nlohmann::json& object, Walk& walk)
  {
    if (object.contains("uuid"))
    {
      walk.objectUUIDs.insert(canonical(object.at("uuid").get<std::string>()));
    }

    if (object.contains("components"))
    {
      for (const auto& component : object.at("components"))
      {
        walk.components.push_back(&component);
      }
    }

    if (object.contains("scripts"))
    {
      for (const auto& script : object.at("scripts"))
      {
        walk.scriptEntries.push_back(&script);
      }
    }

    if (object.contains("children"))
    {
      for (const auto& child : object.at("children"))
      {
        collectObject(child, walk);
      }
    }
  }

  inline bool existsAnywhere(const std::filesystem::path& folder, const Context& context,
                             const std::string& filePath)
  {
    std::error_code ec;
    if (std::filesystem::exists(folder / filePath, ec))
    {
      return true;
    }

    const std::string prefix = "assets/";
    if (filePath.rfind(prefix, 0) == 0)
    {
      return std::filesystem::exists(context.builtInAssets / filePath.substr(prefix.size()), ec);
    }

    return false;
  }

  inline void validateProject(const std::filesystem::path& folder, const Context& context,
                              const std::optional<TemplateManifest>& manifest,
                              std::vector<std::string>& problems)
  {
    std::ifstream file(folder / "project.ecs3d");
    if (!file.is_open())
    {
      problems.push_back("project: project.ecs3d is missing");
      return;
    }

    const auto project = nlohmann::json::parse(file);

    const auto componentRegistry = std::make_shared<ComponentRegistry>();
    registerDataComponents(*componentRegistry);
    {
      AssetRegistry assetRegistry;
      SceneManager sceneManager;
      ProjectSerializer(&assetRegistry, &sceneManager, componentRegistry).deserialize(project);
    }

    const auto& assets = project.at("assets");
    const auto listOf = [&](const char* key) {
      return assets.contains(key) ? assets.at(key) : nlohmann::json::array();
    };

    std::set<std::string> modelUUIDs;
    std::set<std::string> textureUUIDs;
    std::set<std::string> assetUUIDs;
    const auto registerFiles = [&](const char* key, std::set<std::string>& into) {
      for (const auto& asset : listOf(key))
      {
        const auto uuid = canonical(asset.at("uuid").get<std::string>());
        into.insert(uuid);
        assetUUIDs.insert(uuid);

        const auto filePath = asset.at("filePath").get<std::string>();
        if (!staysInside(filePath) || !existsAnywhere(folder, context, filePath))
        {
          problems.push_back("asset-file: missing or outside the folder: " + filePath);
        }
      }
    };

    registerFiles("models", modelUUIDs);
    registerFiles("textures", textureUUIDs);

    const auto scenes = listOf("scenes");
    const auto prefabs = listOf("prefabs");
    const auto scriptAssets = listOf("scripts");
    for (const auto* list : { &scriptAssets, &prefabs, &scenes })
    {
      for (const auto& asset : *list)
      {
        assetUUIDs.insert(canonical(asset.at("uuid").get<std::string>()));
      }
    }

    if (manifest)
    {
      bool foundEntry = false;
      for (const auto& scene : scenes)
      {
        foundEntry = foundEntry || scene.at("name").get<std::string>() == manifest->entryScene;
      }

      if (!foundEntry)
      {
        problems.push_back("entryScene: no scene named " + manifest->entryScene);
      }
    }

    Walk walk;
    for (const auto& scene : scenes)
    {
      for (const auto& object : scene.at("objects"))
      {
        collectObject(object, walk);
      }

      if (scene.contains("scripts"))
      {
        for (const auto& script : scene.at("scripts"))
        {
          walk.scriptEntries.push_back(&script);
        }
      }
    }

    for (const auto& prefab : prefabs)
    {
      const auto name = prefab.value("name", std::string("?"));
      if (!prefab.contains("body") || !prefab.at("body").is_object())
      {
        problems.push_back("prefab: " + name + " has no body");
        continue;
      }

      try
      {
        ObjectManager scratch(componentRegistry);
        scratch.instantiate(prefab.at("body"));
      }
      catch (const std::exception& e)
      {
        problems.push_back("prefab: " + name + " does not instantiate: " + e.what());
        continue;
      }

      collectObject(prefab.at("body"), walk);
    }

    for (const auto* component : walk.components)
    {
      const auto check = [&](const char* key, const std::set<std::string>& known) {
        const auto value = component->value(key, std::string());
        if (!value.empty() && !known.contains(canonical(value)))
        {
          problems.push_back(std::string("reference: ") + key + " " + value + " is not registered");
        }
      };

      if (component->value("type", std::string()) == "ModelRenderer")
      {
        check("modelUUID", modelUUIDs);
        check("textureUUID", textureUUIDs);
        check("specularMapUUID", textureUUIDs);
      }

      check("sourceModel", modelUUIDs);
    }

    const auto builtIn = scriptClassesIn(context.builtInScripts);
    const auto own = scriptClassesIn(folder / "scripts");
    auto classes = builtIn;
    classes.insert(own.begin(), own.end());

    for (const auto& name : own)
    {
      if (builtIn.contains(name))
      {
        problems.push_back("script-class: " + name + " duplicates a built-in script");
      }
    }

    for (const auto& script : scriptAssets)
    {
      if (!classes.contains(script.at("className").get<std::string>()))
      {
        problems.push_back("script-class: unknown " + script.at("className").get<std::string>());
      }
    }

    for (const auto* script : walk.scriptEntries)
    {
      const auto className = script->value("className", std::string());
      if (!classes.contains(className))
      {
        problems.push_back("script-class: unknown " + className);
      }

      if (!script->contains("fields"))
      {
        continue;
      }

      for (const auto& field : script->at("fields"))
      {
        if (!field.contains("value") || !field.at("value").is_string()
            || !isUUID(field.at("value").get<std::string>()))
        {
          continue;
        }

        const auto value = canonical(field.at("value").get<std::string>());
        const auto fieldName = field.value("name", std::string("?"));
        if (walk.objectUUIDs.contains(value))
        {
          problems.push_back("script-uuid: field " + fieldName + " holds an object uuid");
        }
        else if (!assetUUIDs.contains(value))
        {
          problems.push_back("script-uuid: field " + fieldName + " names no registered asset or prefab");
        }
      }
    }
  }

  inline std::vector<std::string> validate(const std::filesystem::path& folder, const Context& context)
  {
    std::vector<std::string> problems;

    std::string error;
    const auto manifest = loadTemplateManifest(folder, &error);
    if (!manifest)
    {
      problems.push_back("manifest: " + error);
    }
    else
    {
      std::error_code ec;
      if (!staysInside(manifest->thumbnail) || !std::filesystem::exists(folder / manifest->thumbnail, ec))
      {
        problems.push_back("thumbnail: missing or outside the folder: " + manifest->thumbnail);
      }
    }

    try
    {
      validateProject(folder, context, manifest, problems);
    }
    catch (const std::exception& e)
    {
      problems.push_back(std::string("project: ") + e.what());
    }

    return problems;
  }
}

#endif //TEMPLATEVALIDATION_H
