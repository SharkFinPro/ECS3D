#ifndef OBJECT_H
#define OBJECT_H

#include <vector>
#include <unordered_map>
#include <memory>

class ObjectManager;

enum class ComponentType;
class Component;

class Object {
public:
  Object();
  explicit Object(const std::vector<std::shared_ptr<Component>>& components);

  void addComponent(std::shared_ptr<Component> component);
  std::shared_ptr<Component> getComponent(ComponentType type) const;

  void variableUpdate(float dt);
  void fixedUpdate(float dt);

  void setManager(ObjectManager* objectManager);
  ObjectManager* getManager() const;

private:
  std::unordered_map<ComponentType, std::shared_ptr<Component>> components;
  ObjectManager* manager;
};



#endif //OBJECT_H
