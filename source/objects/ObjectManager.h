#ifndef OBJECTMANAGER_H
#define OBJECTMANAGER_H

#include <nlohmann/json_fwd.hpp>
#include <vector>
#include <memory>

class Object;
class ECS3D;
class CollisionManager;
class ObjectGUIManager;

class ObjectManager {
public:
  explicit ObjectManager(ECS3D* ecs);

  void fixedUpdate(float dt) const;

  void variableUpdate();

  [[nodiscard]] ECS3D* getECS() const;

  [[nodiscard]] std::shared_ptr<CollisionManager> getCollisionManager() const;

  void addObject(const std::shared_ptr<Object>& object);

  void duplicateObject(const std::shared_ptr<Object>& object);

  void start() const;

  void stop() const;

  [[nodiscard]] nlohmann::json serialize() const;

  void removeObject(const std::shared_ptr<Object>& object);

private:
  ECS3D* m_ecs = nullptr;

  std::vector<std::shared_ptr<Object>> m_objects;

  std::vector<std::shared_ptr<Object>> m_objectsToRemove;

  std::shared_ptr<CollisionManager> m_collisionManager;

  std::shared_ptr<ObjectGUIManager> m_objectGUIManager;

  void deleteObjectsMarkedForDeletion();
};



#endif //OBJECTMANAGER_H
