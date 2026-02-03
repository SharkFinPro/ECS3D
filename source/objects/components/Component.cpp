#include "Component.h"
#include "../Object.h"
#include <imgui.h>

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

void Component::variableUpdate([[maybe_unused]] float dt)
{}

void Component::fixedUpdate([[maybe_unused]] float dt)
{}

void Component::displayGui()
{}

bool Component::markedAsDeleted() const
{
  return m_shouldDelete;
}

void Component::markAsDeleted()
{
  m_shouldDelete = true;
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
  const char* componentDisplayName = componentTypeToString.at(m_subType != ComponentType::SubComponentType_none
                                                              ? m_subType : m_type).c_str();

  const bool open = ImGui::CollapsingHeader(
    componentDisplayName,
    ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_DefaultOpen
  );

  if (m_type != ComponentType::transform)
  {
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
  }

  return open;
}

void Component::loadVariable(ComponentVariableBase& variable)
{
  m_variables.emplace_back(&variable);
}
