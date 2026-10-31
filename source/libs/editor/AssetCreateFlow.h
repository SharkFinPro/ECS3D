#ifndef ASSETCREATEFLOW_H
#define ASSETCREATEFLOW_H

#include "AssetNaming.h"
#include <assets/AssetRegistry.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <uuid.h>

// The decisions behind the asset browser's create-and-name modal: the pending request, name validation, the
// addAsset blob per type and the scene activated after a create. The panel draws, copies and writes files.
class AssetCreateFlow {
public:
  enum class Type { None, Model, Texture, Scene, Script, Prefab };

  // A model/texture import or scene/script/prefab creation awaiting a name.
  struct PendingAsset {
    Type type = Type::None;
    std::string sourcePath; // the picked file, for Model/Texture
    char name[128] = {};
  };

  using FileExists = std::function<bool(const std::string& name)>;

  // New scripts go in the project's own scripts/ folder; a project with no folder keeps the working dir's.
  [[nodiscard]] static std::filesystem::path scriptDirectory(const std::filesystem::path& projectRoot)
  {
    return projectRoot.empty() ? std::filesystem::path("scripts/UserScripts") : projectRoot / "scripts";
  }

  [[nodiscard]] static bool nameIsValid(const std::string& name)
  {
    return !name.empty() && std::ranges::none_of(name, [](const char c) {
      return c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
    });
  }

  [[nodiscard]] static bool writesFiles(const Type type)
  {
    return type == Type::Model || type == Type::Texture || type == Type::Script;
  }

  // Replaces the pending request and asks for the modal to open.
  void begin(const Type type, const std::string& sourcePath, const std::string& defaultName)
  {
    m_pending = {};
    m_pending.type = type;
    m_pending.sourcePath = sourcePath;
    std::strncpy(m_pending.name, defaultName.c_str(), sizeof(m_pending.name) - 1);
    m_error.clear();
    m_openRequested = true;
  }

  // True once per begin().
  [[nodiscard]] bool takeOpenRequest()
  {
    return std::exchange(m_openRequested, false);
  }

  [[nodiscard]] PendingAsset& pending()
  {
    return m_pending;
  }

  [[nodiscard]] const PendingAsset& pending() const
  {
    return m_pending;
  }

  [[nodiscard]] const std::string& error() const
  {
    return m_error;
  }

  // Checks the typed name, leaving the reason in error() when it is refused. fileExists reports whether a
  // script file of that name is already on disk, and is asked only when the registry has not already refused.
  [[nodiscard]] bool validate(const AssetRegistry& registry, const FileExists& fileExists)
  {
    const std::string name = m_pending.name;

    if (!nameIsValid(name))
    {
      m_error = "Enter a valid name (no path characters).";
    }
    else if (m_pending.type == Type::Script &&
             (assetNaming::isNameTaken(registry, AssetType::Script, name) || (fileExists && fileExists(name))))
    {
      m_error = "A script named " + name + " already exists.";
    }
    else if ((m_pending.type == Type::Prefab || m_pending.type == Type::Scene) &&
             assetNaming::isNameTaken(registry, AssetType::Prefab, name))
    {
      m_error = "A scene or prefab named " + name + " already exists.";
    }
    else
    {
      return true;
    }

    return false;
  }

  // The addAsset blob for the pending request. importedPath is the project-relative path a model or texture
  // was copied to; prefabObjectUUID names the object in a new prefab's body.
  [[nodiscard]] nlohmann::json addAssetJson(const uuids::uuid& assetUUID, const std::string& importedPath,
                                            const std::filesystem::path& projectRoot,
                                            const std::string& prefabObjectUUID) const
  {
    const std::string uuid = uuids::to_string(assetUUID);
    const std::string name = m_pending.name;

    switch (m_pending.type)
    {
      case Type::Model:
        return { { "assetType", "model" }, { "uuid", uuid }, { "path", importedPath } };
      case Type::Texture:
        return { { "assetType", "texture" }, { "uuid", uuid }, { "path", importedPath } };
      case Type::Scene:
        return { { "assetType", "scene" }, { "uuid", uuid }, { "name", name } };
      case Type::Prefab:
        return { { "assetType", "prefab" }, { "uuid", uuid }, { "name", name },
                 { "body", assetNaming::emptyPrefabBody(name, prefabObjectUUID) } };
      case Type::Script:
      {
        const std::filesystem::path file = name + ".cs";
        const auto directory = projectRoot.empty() ? scriptDirectory(projectRoot) : std::filesystem::path("scripts");
        const auto path = directory / file;
        return { { "assetType", "script" }, { "uuid", uuid }, { "path", path.generic_string() },
                 { "className", name } };
      }
      case Type::None:
        break;
    }

    return {};
  }

  // The asset was created and sent. A scene is remembered for takeSceneToActivate().
  void committed(const uuids::uuid& assetUUID)
  {
    if (m_pending.type == Type::Scene)
    {
      m_sceneToActivate = assetUUID;
    }

    m_pending = {};
  }

  // The request stays, so the form remains open for another try.
  void commitFailed(const std::string& message)
  {
    m_error = message;
  }

  void cancel()
  {
    m_pending = {};
  }

  // The scene committed() remembered, once. A newly created scene becomes the active one, but not from the
  // commit: that runs inside the create popup's error boundary, and a throw from the load would leave a
  // created-and-sent scene looking like a failed create - with the form still open, so pressing Create
  // again would mint a second one. The caller fires it once the commit can no longer fail.
  [[nodiscard]] std::optional<uuids::uuid> takeSceneToActivate()
  {
    return std::exchange(m_sceneToActivate, std::nullopt);
  }

private:
  PendingAsset m_pending;
  bool m_openRequested = false;
  std::string m_error;
  std::optional<uuids::uuid> m_sceneToActivate;
};

#endif //ASSETCREATEFLOW_H
