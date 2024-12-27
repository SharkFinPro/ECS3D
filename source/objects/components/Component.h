#ifndef COMPONENT_H
#define COMPONENT_H

#include <string>
#include <unordered_map>

class Object;

enum class ComponentType {
  transform,
  modelRenderer,
  rigidBody,
  collider,
  player,
  lightRenderer,
  SubComponentType_none,
  SubComponentType_boxCollider,
  SubComponentType_sphereCollider
};

const std::unordered_map<ComponentType, std::string> componentTypeToString {
  {ComponentType::transform, "Transform"},
  {ComponentType::modelRenderer, "Model Renderer"},
  {ComponentType::rigidBody, "Rigid Body"},
  {ComponentType::SubComponentType_boxCollider, "Box Collider"},
  {ComponentType::SubComponentType_sphereCollider, "Sphere Collider"},
  {ComponentType::player, "Player"},
  {ComponentType::lightRenderer, "Light Renderer"}
};

const std::unordered_map<ComponentType, ComponentType> subComponentTypeToParent {
  {ComponentType::SubComponentType_boxCollider, ComponentType::collider},
  {ComponentType::SubComponentType_sphereCollider, ComponentType::collider}
};

class Component {
public:
  explicit Component(ComponentType type, ComponentType subType = ComponentType::SubComponentType_none);
  virtual ~Component() = default;

  [[nodiscard]] ComponentType getType() const;

  [[nodiscard]] ComponentType getSubType() const;

  void setOwner(Object* owner);
  [[nodiscard]] Object* getOwner() const;

  virtual void variableUpdate(float dt);
  virtual void fixedUpdate(float dt);

  virtual void displayGui();

  [[nodiscard]] bool markedAsDeleted() const;

  void markAsDeleted();

  virtual void start();

  virtual void stop();

protected:
  ComponentType type;
  ComponentType subType;
  Object* owner;

  bool shouldDelete;

  [[nodiscard]] bool displayGuiHeader();
};



#endif //COMPONENT_H
