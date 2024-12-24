#ifndef OBJECT_H
#define OBJECT_H

#include <vector>
#include <unordered_map>
#include <memory>
#include <string>

#include "ObjectManager.h"

enum class ComponentType;
class Component;

class Object {
public:
  explicit Object(std::string name = "Object");
  explicit Object(const std::vector<std::shared_ptr<Component>>& components, std::string name = "Object");

  void setParent(const std::shared_ptr<Object>& parent);
  [[nodiscard]] std::shared_ptr<Object> getParent() const;

  void setUINode(const std::shared_ptr<ObjectUINode> &uiNode);
  [[nodiscard]] std::shared_ptr<ObjectUINode> getUINode() const;

  void addComponent(const std::shared_ptr<Component>& component, bool setOwner = true);

  [[nodiscard]] std::shared_ptr<Component> getComponent(ComponentType type) const;

  void variableUpdate(float dt);
  void fixedUpdate(float dt);

  void setManager(ObjectManager* objectManager);
  [[nodiscard]] ObjectManager* getManager() const;

  [[nodiscard]] std::string getName() const;
  void setName(const std::string& name);

  void displayGui();

  void reset();

private:
  std::unordered_map<ComponentType, std::shared_ptr<Component>> components;
  ObjectManager* manager;

  std::shared_ptr<Object> parent;
  std::shared_ptr<ObjectUINode> uiNode;

  std::string name;

  bool showComponentSelector;
};



#endif //OBJECT_H
