#include "Component.h"
#include "../Object.h"
#include <imgui.h>

Component::Component(const ComponentType type, const ComponentType subType)
  : type(type), subType(subType), owner(nullptr), shouldDelete(false)
{}

ComponentType Component::getSubType() const
{
  return subType;
}

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

bool Component::markedAsDeleted() const
{
  return shouldDelete;
}

void Component::markAsDeleted()
{
  shouldDelete = true;
}

void Component::start() const
{
  for (auto* variable : m_variables)
  {
    variable->start();
  }
}

void Component::stop() const
{
  for (auto* variable : m_variables)
  {
    variable->stop();
  }
}

bool Component::displayGuiHeader()
{
  const char* componentDisplayName = componentTypeToString.at(subType != ComponentType::SubComponentType_none
                                                              ? subType : type).c_str();

  const bool open = ImGui::CollapsingHeader(componentDisplayName, ImGuiTreeNodeFlags_AllowOverlap);

  ImGui::SameLine();

  const float buttonWidth = ImGui::CalcTextSize("-").x + ImGui::GetStyle().FramePadding.x * 4.0f;
  const float contentRegionWidth = ImGui::GetContentRegionAvail().x;

  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + contentRegionWidth - buttonWidth);

  ImGui::PushID(this);
  if (ImGui::Button("-", {buttonWidth, 0}))
  {
    markAsDeleted();
  }
  ImGui::PopID();

  return open;
}

void Component::loadVariable(ComponentVariableBase& variable)
{
  m_variables.emplace_back(&variable);
}
