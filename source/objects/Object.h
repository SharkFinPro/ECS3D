#ifndef OBJECT_H
#define OBJECT_H

#include <vector>
#include <unordered_map>
#include <memory>
#include <string>

class ObjectManager;

enum class ComponentType;
class Component;

class Object {
public:
  explicit Object(std::string name = "Object");
  explicit Object(const std::vector<std::shared_ptr<Component>>& components, std::string name = "Object");

  void addComponent(std::shared_ptr<Component> component);
  [[nodiscard]] std::shared_ptr<Component> getComponent(ComponentType type) const;

  void variableUpdate(float dt);
  void fixedUpdate(float dt);

  void setManager(ObjectManager* objectManager);
  [[nodiscard]] ObjectManager* getManager() const;

  [[nodiscard]] std::string getName() const;
  void setName(const std::string& name);

  void displayGui() const;

private:
  std::unordered_map<ComponentType, std::shared_ptr<Component>> components;
  ObjectManager* manager;

  std::string name;
};



#endif //OBJECT_H
