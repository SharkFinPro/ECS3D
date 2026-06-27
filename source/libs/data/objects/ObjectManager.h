#ifndef OBJECTMANAGER_H
#define OBJECTMANAGER_H

#include <nlohmann/json_fwd.hpp>
#include <memory>
#include <random>
#include <vector>
#include <uuid.h>

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

  void start() const;

  void stop() const;

  [[nodiscard]] nlohmann::json serialize() const;

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
};



#endif //OBJECTMANAGER_H
