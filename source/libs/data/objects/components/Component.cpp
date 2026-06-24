#include "Component.h"

Component::Component(const ComponentType type, const ComponentType subType)
  : m_type(type), m_subType(subType)
{}

ComponentType Component::getSubType() const
{
  return m_subType;
}

ComponentType Component::getType() const
{
  return m_type;
}

void Component::setOwner(Object* owner)
{
  m_owner = owner;
}

Object* Component::getOwner() const
{
  return m_owner;
}

bool Component::markedAsDeleted() const
{
  return m_shouldDelete;
}

void Component::markAsDeleted()
{
  m_shouldDelete = true;
}

void Component::start()
{
  for (auto* variable : m_variables)
  {
    variable->start();
  }
}

void Component::stop()
{
  for (auto* variable : m_variables)
  {
    variable->stop();
  }
}

void Component::loadVariable(ComponentVariableBase& variable)
{
  m_variables.emplace_back(&variable);
}
