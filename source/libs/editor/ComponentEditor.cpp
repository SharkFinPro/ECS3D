#include "ComponentEditor.h"
#include <objects/components/Component.h>
#include <imgui.h>

void ComponentEditor::registerHandler(const std::string& typeName, GuiHandler handler)
{
  m_handlers[typeName] = std::move(handler);
}

bool ComponentEditor::displayGui(const std::string& typeName, const std::shared_ptr<Component>& component) const
{
  if (const auto it = m_handlers.find(typeName); it != m_handlers.end())
  {
    return it->second(component);
  }

  return false;
}

bool ComponentEditor::displayHeader(const std::shared_ptr<Component>& component, const std::string& componentName)
{
  auto componentDisplayName = componentTypeToString.at(
    component->getSubType() != ComponentType::SubComponentType_none ? component->getSubType() : component->getType());

  if (!componentName.empty())
  {
    componentDisplayName = componentName;
  }

  const bool open = ImGui::CollapsingHeader(
    componentDisplayName.c_str(),
    ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_DefaultOpen
  );

  if (component->getType() != ComponentType::transform)
  {
    ImGui::SameLine();

    const float buttonWidth = ImGui::CalcTextSize("-").x + ImGui::GetStyle().FramePadding.x * 4.0f;
    const float contentRegionWidth = ImGui::GetContentRegionAvail().x;

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + contentRegionWidth - buttonWidth);

    ImGui::PushID(component.get());
    if (ImGui::Button("-", {buttonWidth, 0}))
    {
      component->markAsDeleted();
    }
    ImGui::PopID();
  }

  return open;
}
