#include "Component.h"

Component::Component(ComponentType type)
  : type(type), owner(nullptr)
{}

ComponentType Component::getType() const
{
  return type;
}

void Component::setOwner(Object* owner)
{
  this->owner = owner;
}

Object* Component::getOwner() const
{
  return owner;
}

void Component::variableUpdate([[maybe_unused]] float dt)
{}

void Component::fixedUpdate([[maybe_unused]] float dt)
{}
