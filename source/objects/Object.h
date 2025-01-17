#ifndef OBJECT_H
#define OBJECT_H

#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include "ObjectManager.h"

enum class ComponentType;
class Component;

class Object : public std::enable_shared_from_this<Object> {
public:
  explicit Object(std::string name = "Object");
  explicit Object(const std::vector<std::shared_ptr<Component>>& components, std::string name = "Object");

  void setParent(const std::shared_ptr<Object>& parent);
  [[nodiscard]] std::shared_ptr<Object> getParent() const;

  void addComponent(const std::shared_ptr<Component>& component, bool setOwner = true);

  template<typename T>
  [[nodiscard]] std::shared_ptr<T> getComponent(ComponentType type) const;

  void variableUpdate(float dt);
  void fixedUpdate(float dt);

  void setManager(ObjectManager* objectManager);
  [[nodiscard]] ObjectManager* getManager() const;

  [[nodiscard]] std::string getName() const;
  void setName(const std::string& name);

  void displayGui();

  void start() const;

  void stop() const;

private:
  std::unordered_map<ComponentType, std::shared_ptr<Component>> components;
  ObjectManager* manager;

  std::shared_ptr<Object> parent;

  std::string name;

  bool showComponentSelector;

  [[nodiscard]] std::shared_ptr<Component> getComponent(ComponentType type) const;
};


template<typename T>
std::shared_ptr<T> Object::getComponent(const ComponentType type) const
{
  const auto component = getComponent(type);

  return component ? std::dynamic_pointer_cast<T>(component) : nullptr;
}

#endif //OBJECT_H
