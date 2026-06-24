#ifndef OBJECT_H
#define OBJECT_H

#include "ObjectManager.h"
#include <nlohmann/json_fwd.hpp>
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include <uuid.h>

enum class ComponentType;
class Component;

class Object : public std::enable_shared_from_this<Object> {
public:
  explicit Object(std::string name = "Object");

  explicit Object(const std::vector<std::shared_ptr<Component>>& components,
                  std::string name = "Object");

  Object(const nlohmann::json& objectData,
         ObjectManager* manager);

  void loadChildren(const nlohmann::json& childrenData);

  void setParent(const std::shared_ptr<Object>& parent);

  [[nodiscard]] std::shared_ptr<Object> getParent() const;

  void addChild(std::shared_ptr<Object> child);

  void removeChild(const std::shared_ptr<Object>& child);

  [[nodiscard]] const std::vector<std::shared_ptr<Object>>& getChildren() const;

  void addComponent(const std::shared_ptr<Component>& component,
                    bool setOwner = true);

  template<typename T>
  [[nodiscard]] std::shared_ptr<T> getComponent(ComponentType type) const;

  void setManager(ObjectManager* objectManager);
  [[nodiscard]] ObjectManager* getManager() const;

  [[nodiscard]] std::string getName() const;
  void setName(const std::string& name);

  void start() const;

  void stop() const;

  [[nodiscard]] nlohmann::json serialize();

  [[nodiscard]] uuids::uuid getUUID() const;

  [[nodiscard]] bool isAncestorOf(const std::shared_ptr<Object>& object) const;

  [[nodiscard]] const std::unordered_map<ComponentType, std::shared_ptr<Component>>& getComponents() const;

  [[nodiscard]] const std::vector<std::shared_ptr<Component>>& getScripts() const;

private:
  std::unordered_map<ComponentType, std::shared_ptr<Component>> m_components;
  std::vector<std::shared_ptr<Component>> m_scripts;

  ObjectManager* m_manager = nullptr;

  std::weak_ptr<Object> m_parent;

  std::vector<std::shared_ptr<Object>> m_children;

  uuids::uuid m_uuid;

  std::string m_name;

  [[nodiscard]] std::shared_ptr<Component> getComponent(ComponentType type) const;

  void loadFromJSON(const nlohmann::json& objectData);
};


template<typename T>
std::shared_ptr<T> Object::getComponent(const ComponentType type) const
{
  const auto component = getComponent(type);

  return component ? std::dynamic_pointer_cast<T>(component) : nullptr;
}

#endif //OBJECT_H
