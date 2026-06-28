#ifndef SCENEASSET_H
#define SCENEASSET_H

#include <nlohmann/json_fwd.hpp>
#include <memory>
#include <string>
#include <uuid.h>

namespace net {
  class Message;
  class MessageReader;
}

class ObjectManager;
class ComponentRegistry;

// A scene is data: a named ObjectManager (the object tree). It is no longer part of the polymorphic
// Asset hierarchy (the file assets are flat records in AssetRegistry); the verbs moved to the systems
// and displayGui to the editor. start/stop stay (data lifecycle for the play/stop ComponentVariables).
class SceneAsset {
public:
  SceneAsset(uuids::uuid uuid,
             std::string name,
             const std::shared_ptr<ComponentRegistry>& componentRegistry);

  void loadObjects(const nlohmann::json& objectsData) const;

  void start() const;

  void stop() const;

  [[nodiscard]] nlohmann::json serialize() const;

  void pack(net::Message& message) const;

  // Reconstructs a scene (uuid + name + object tree) from a packed snapshot. A static factory because
  // the uuid/name lead the packed data and are needed to construct the SceneAsset itself.
  [[nodiscard]] static std::shared_ptr<SceneAsset> unpack(net::MessageReader& messageReader,
                                                          const std::shared_ptr<ComponentRegistry>& componentRegistry);

  [[nodiscard]] std::shared_ptr<ObjectManager> getObjectManager() const;

  [[nodiscard]] uuids::uuid getUUID() const;

  [[nodiscard]] std::string getName() const;

private:
  uuids::uuid m_uuid;

  std::string m_name;

  std::shared_ptr<ObjectManager> m_objectManager;
};



#endif //SCENEASSET_H
