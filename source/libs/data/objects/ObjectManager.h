#ifndef OBJECTMANAGER_H
#define OBJECTMANAGER_H

#include <nlohmann/json_fwd.hpp>
#include <memory>
#include <random>
#include <vector>
#include <uuid.h>

namespace net {
  class Message;
  class MessageReader;
}

class Object;
class ComponentRegistry;

class ObjectManager {
public:
  explicit ObjectManager(std::shared_ptr<ComponentRegistry> componentRegistry);

  [[nodiscard]] std::shared_ptr<ComponentRegistry> getComponentRegistry() const;

  [[nodiscard]] uuids::uuid createUUID();

  void addObject(const std::shared_ptr<Object>& object);

  void addObjectToRoot(const std::shared_ptr<Object>& object);

  void removeObjectFromRoot(const std::shared_ptr<Object>& object);

  void duplicateObject(const std::shared_ptr<Object>& object);

  // Build a live object (and its whole subtree) from a serialized-object blob, giving every node a fresh
  // uuid, and add it at the scene root. This is the prefab instantiation path (a prefab's body is one
  // Object::serialize() blob — see AssetRegistry::getPrefabBody) and the shared core of duplicateObject.
  //
  // Throws if objectData isn't a well-formed serialized object (unknown component type, missing uuid/name);
  // callers on the tick loop must guard. The returned object is NOT started — a caller spawning into a
  // running scene starts it (and its children) itself.
  std::shared_ptr<Object> instantiate(const nlohmann::json& objectData);

  void start() const;

  void stop() const;

  [[nodiscard]] nlohmann::json serialize() const;

  void pack(net::Message& message) const;

  void unpack(net::MessageReader& messageReader);

  void removeObject(const std::shared_ptr<Object>& object);

  void deleteObjectsMarkedForDeletion();

  [[nodiscard]] std::shared_ptr<Object> getObjectByUUID(uuids::uuid uuid) const;

  [[nodiscard]] const std::vector<std::shared_ptr<Object>>& getObjects() const;

  [[nodiscard]] const std::vector<std::shared_ptr<Object>>& getAllObjects() const;

private:
  std::shared_ptr<ComponentRegistry> m_componentRegistry;

  std::vector<std::shared_ptr<Object>> m_objects;

  std::vector<std::shared_ptr<Object>> m_allObjects;

  std::vector<std::shared_ptr<Object>> m_objectsToRemove;

  std::mt19937 m_rng;
  uuids::uuid_random_generator m_uuidGenerator;

  // Recursively replace the serialized object's (and its children's) uuids with fresh ones.
  void reassignUUIDs(nlohmann::json& objectData);

  // Shared body of instantiate/duplicateObject: fresh uuids, build the root under `parent` (null = scene
  // root), then build its children.
  std::shared_ptr<Object> instantiateUnder(const nlohmann::json& objectData,
                                           const std::shared_ptr<Object>& parent);
};



#endif //OBJECTMANAGER_H
