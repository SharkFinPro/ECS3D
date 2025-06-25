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

class ComponentVariableBase {
public:
  virtual ~ComponentVariableBase() = default;

  virtual void start()
  {
    m_live = true;
  }

  void stop()
  {
    m_live = false;
  }

protected:
  bool m_live = true;
};

template <typename T>
class ComponentVariable final : public ComponentVariableBase {
public:
  explicit ComponentVariable(const T& initialValue)
    : m_initialValue(initialValue), m_liveValue(initialValue)
  {}

  void start() override
  {
    ComponentVariableBase::start();

    m_liveValue = m_initialValue;
  }

  [[nodiscard]] T& value()
  {
    return m_live ? m_liveValue : m_initialValue;
  }

  [[nodiscard]] T get() const
  {
    return m_live ? m_liveValue : m_initialValue;
  }

  void set(const T& newValue)
  {
    if (m_live)
    {
      m_liveValue = newValue;
    }
    else
    {
      m_initialValue = newValue;
    }
  }

private:
  T m_initialValue;
  T m_liveValue;
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

  void start() const;

  void stop() const;

protected:
  ComponentType type;
  ComponentType subType;
  Object* owner;

  bool shouldDelete;

  std::vector<ComponentVariableBase*> m_variables;

  [[nodiscard]] bool displayGuiHeader();

  void loadVariable(ComponentVariableBase& variable);
};



#endif //COMPONENT_H
