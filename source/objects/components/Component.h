#ifndef COMPONENT_H
#define COMPONENT_H

#include <array>
#include <string>

class Object;

enum class ComponentType {
  transform,
  modelRenderer,
  rigidBody,
  collider,
  player,
  lightRenderer
};

inline std::array<std::pair<ComponentType, std::string>, 7> allComponentTypes {
  std::make_pair(ComponentType::transform, "Transform"),
  std::make_pair(ComponentType::modelRenderer, "Model Renderer"),
  std::make_pair(ComponentType::rigidBody, "Rigid Body"),
  std::make_pair(ComponentType::collider, "Box Collider"),
  std::make_pair(ComponentType::collider, "Sphere Collider"),
  std::make_pair(ComponentType::player, "Player"),
  std::make_pair(ComponentType::lightRenderer, "Light Renderer")
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
};



#endif //COMPONENT_H
