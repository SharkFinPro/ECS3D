#include "Component.h"
#include <imgui.h>

#include "../Object.h"

Component::Component(const ComponentType type)
  : type(type), owner(nullptr), shouldDelete(false)
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

bool Component::markedAsDeleted() const
{
  return shouldDelete;
}

void Component::markAsDeleted()
{
  shouldDelete = true;
}

bool Component::displayGuiHeader()
{
  const bool open = ImGui::CollapsingHeader(componentTypeToString.at(type).c_str(),
                                            ImGuiTreeNodeFlags_AllowOverlap);

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
