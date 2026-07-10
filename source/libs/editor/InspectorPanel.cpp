#include "InspectorPanel.h"
#include "GuiComponents.h"
#include "ObjectInspector.h"
#include "Selection.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <imgui.h>
#include <utility>

InspectorPanel::InspectorPanel(std::shared_ptr<ComponentEditor> componentEditor)
  : m_objectInspector(std::make_unique<ObjectInspector>(std::move(componentEditor)))
{}

// Defined here (not defaulted in the header) so the ObjectInspector unique_ptr sees a complete type.
InspectorPanel::~InspectorPanel() = default;

void InspectorPanel::setSelection(std::shared_ptr<EditorSelection> selection)
{
  m_selection = std::move(selection);
}

void InspectorPanel::setAssetRegistry(const AssetRegistry* registry)
{
  m_objectInspector->setAssetRegistry(registry);
}

void InspectorPanel::setEditable(const bool editable)
{
  m_objectInspector->setEditable(editable);
}

void InspectorPanel::setEditCallback(EditCallback callback)
{
  m_objectInspector->setEditCallback(std::move(callback));
}

void InspectorPanel::setSceneEditCallback(SceneEditCallback callback)
{
  m_objectInspector->setSceneEditCallback(std::move(callback));
}

void InspectorPanel::displayGui(const ObjectManager* objectManager)
{
  ImGui::Begin("Inspector");

  // Resolve the current selection to a concrete inspector. Only the Object kind is renderable today;
  // objectUUID() returns nullopt for the None/Asset kinds, so those fall through to the empty state.
  const auto selectedUUID = m_selection->objectUUID();
  const auto object = (objectManager && selectedUUID.has_value())
    ? objectManager->getObjectByUUID(selectedUUID.value()) : nullptr;

  // Panel header: small-caps section label + a right-aligned per-kind type chip (mockup).
  gc::sectionLabel("Inspector");
  if (object)
  {
    m_objectInspector->displayTypeChip(object);
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Empty state when nothing renderable is selected — an intentional placeholder rather than a lone
  // checkbox. The copy is kind-agnostic (objects today, assets in a later phase).
  if (!object)
  {
    if (selectedUUID.has_value())
    {
      // Selection referenced an object that no longer exists (e.g. a fresh snapshot replaced the scene).
      m_selection->clear();
    }

    gc::emptyState(gc::SecIcon::block, "Nothing selected",
                   "Select an object or asset to inspect it");

    ImGui::End();
    return;
  }

  m_objectInspector->display(object);

  ImGui::End();
}

std::optional<uuids::uuid> InspectorPanel::getHighlightUUID() const
{
  // Only object selections highlight in the viewport; objectUUID() already returns nullopt for the
  // None/Asset kinds, so this naturally suppresses the highlight for those.
  return m_objectInspector->highlightEnabled() ? m_selection->objectUUID() : std::nullopt;
}
