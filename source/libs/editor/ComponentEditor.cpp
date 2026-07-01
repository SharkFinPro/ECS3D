#include "ComponentEditor.h"
#include "GuiComponents.h"
#include <objects/components/Component.h>
#include <imgui.h>

namespace {
  gc::SecIcon iconForComponent(const ComponentType type)
  {
    switch (type)
    {
      case ComponentType::transform:      return gc::SecIcon::transform;
      case ComponentType::modelRenderer:  return gc::SecIcon::image;
      case ComponentType::lightRenderer:  return gc::SecIcon::image;
      case ComponentType::rigidBody:      return gc::SecIcon::rigid;
      case ComponentType::collider:       return gc::SecIcon::collider;
      default:                            return gc::SecIcon::none;
    }
  }
}

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

  // Transform is intrinsic to every object, so it has no remove button.
  const bool removable = component->getType() != ComponentType::transform;

  ImGui::PushID(component.get());
  bool removeClicked = false;
  const bool open = gc::sectionHeader(componentDisplayName.c_str(), removable, &removeClicked,
                                      iconForComponent(component->getType()));
  if (removeClicked)
  {
    component->markAsDeleted();
  }
  ImGui::PopID();

  return open;
}
