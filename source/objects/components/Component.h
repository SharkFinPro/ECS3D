#ifndef COMPONENT_H
#define COMPONENT_H

#include <array>
#include <string>
#include <unordered_map>

class Object;

enum class ComponentType {
  transform,
  modelRenderer,
  rigidBody,
  collider,
  player,
  lightRenderer
};

inline std::unordered_map<ComponentType, std::string> componentTypeToString {
  {ComponentType::transform, "Transform"},
  {ComponentType::modelRenderer, "Model Renderer"},
  {ComponentType::rigidBody, "Rigid Body"},
  {ComponentType::collider, "Box Collider"},
  {ComponentType::collider, "Sphere Collider"},
  {ComponentType::player, "Player"},
  {ComponentType::lightRenderer, "Light Renderer"}
};

class Component {
public:
  explicit Component(ComponentType type);
  virtual ~Component() = default;

  [[nodiscard]] ComponentType getType() const;

  void setOwner(Object* owner);
  [[nodiscard]] Object* getOwner() const;

  virtual void variableUpdate(float dt);
  virtual void fixedUpdate(float dt);

  virtual void displayGui();

  virtual void reset();

protected:
  ComponentType type;
  Object* owner;

  [[nodiscard]] bool displayGuiHeader() const;
};



#endif //COMPONENT_H
