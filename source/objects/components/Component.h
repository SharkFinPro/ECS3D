#ifndef COMPONENT_H
#define COMPONENT_H

class Object;

enum class ComponentType {
  transform,
  modelRenderer,
  rigidBody,
  collider,
  player,
  lightRenderer
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
