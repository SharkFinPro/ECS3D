#include "ComponentEditor.h"

void ComponentEditor::registerHandler(const std::string& typeName, GuiHandler handler)
{
  // TODO: register the ImGui editing widget for a component type. This is the central place the
  // TODO:   per-component displayGui() bodies (Transform, RigidBody, ModelRenderer, LightRenderer,
  // TODO:   Collider, Script) move into, instead of living as virtual methods on Component.
  m_handlers[typeName] = std::move(handler);
}

void ComponentEditor::displayGui(const std::string& typeName, const std::shared_ptr<Component>& component) const
{
  // TODO: look up the handler for typeName and draw the component's editing widgets.
  if (const auto it = m_handlers.find(typeName); it != m_handlers.end())
  {
    it->second(component);
  }
}

bool ComponentEditor::displayHeader(const std::shared_ptr<Component>& component, const std::string& componentName)
{
  // TODO: migrate Component::displayGuiHeader here (it left the data-only Component): draw the
  // TODO:   ImGui::CollapsingHeader for the component's type name, and the "-" delete button that
  // TODO:   calls component->markAsDeleted() for everything except Transform. Return whether the
  // TODO:   header is open so the handler knows to draw the body.
  (void)component;
  (void)componentName;

  return false;
}
