#include "Component.h"
#include <imgui.h>

Component::Component(const ComponentType type)
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

void Component::displayGui()
{}

void Component::reset()
{}

bool Component::displayGuiHeader() const
{
  return ImGui::CollapsingHeader(componentTypeToString.at(type).c_str());
}
