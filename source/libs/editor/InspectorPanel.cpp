#include "InspectorPanel.h"
#include "AssetInspector.h"
#include "GuiComponents.h"
#include "ObjectInspector.h"
#include "Selection.h"
#include <assets/AssetRegistry.h>
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <imgui.h>
#include <utility>

InspectorPanel::InspectorPanel(std::shared_ptr<ComponentEditor> componentEditor,
                               std::shared_ptr<GpuAssetCache> assetCache)
  : m_objectInspector(std::make_unique<ObjectInspector>(std::move(componentEditor))),
    m_assetInspector(std::make_unique<AssetInspector>(std::move(assetCache)))
{}

// Defined here (not defaulted in the header) so the inspector unique_ptrs see their complete types.
InspectorPanel::~InspectorPanel() = default;

void InspectorPanel::setSelection(std::shared_ptr<EditorSelection> selection)
{
  m_selection = std::move(selection);
}

void InspectorPanel::setAssetRegistry(const AssetRegistry* registry)
{
  m_assetRegistry = registry;
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

  // Drop a stale asset selection when the registry changed under us and the uuid is gone (deletion in a
  // later phase, or a fresh snapshot). Gated on the registry version so the membership re-check only
  // runs when the registry actually changed, matching the asset browser's cache gating.
  if (const auto assetUUID = m_selection->assetUUID())
  {
    const size_t version = m_assetRegistry ? m_assetRegistry->getVersion() : 0;
    if (version != m_lastAssetRegistryVersion)
    {
      m_lastAssetRegistryVersion = version;
      if (!m_assetRegistry || !m_assetRegistry->getByUUID(assetUUID.value()))
      {
        m_selection->clear();
      }
    }
  }

  // Resolve the current selection to a concrete inspector. A None kind (or a stale one cleared above)
  // leaves both null and falls through to the empty state.
  const auto selectedObjectUUID = m_selection->objectUUID();
  const auto object = (objectManager && selectedObjectUUID.has_value())
    ? objectManager->getObjectByUUID(selectedObjectUUID.value()) : nullptr;

  const auto selectedAssetUUID = m_selection->assetUUID();
  const AssetRecord* asset = (m_assetRegistry && selectedAssetUUID.has_value())
    ? m_assetRegistry->getByUUID(selectedAssetUUID.value()) : nullptr;

  // Panel header: small-caps section label + a right-aligned per-kind type chip (mockup).
  gc::sectionLabel("Inspector");
  if (object)
  {
    m_objectInspector->displayTypeChip(object);
  }
  else if (asset)
  {
    m_assetInspector->displayTypeChip(*asset);
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Empty state when nothing renderable is selected — an intentional placeholder rather than a lone
  // checkbox. The copy is kind-agnostic (objects or assets).
  if (!object && !asset)
  {
    if (selectedObjectUUID.has_value())
    {
      // Selection referenced an object that no longer exists (e.g. a fresh snapshot replaced the scene).
      m_selection->clear();
    }

    gc::emptyState(gc::SecIcon::block, "Nothing selected",
                   "Select an object or asset to inspect it");

    ImGui::End();
    return;
  }

  if (object)
  {
    m_objectInspector->display(object);
  }
  else
  {
    m_assetInspector->display(*asset);
  }

  ImGui::End();
}

std::optional<uuids::uuid> InspectorPanel::getHighlightUUID() const
{
  // Only object selections highlight in the viewport; objectUUID() already returns nullopt for the
  // None/Asset kinds, so this naturally suppresses the highlight for those.
  return m_objectInspector->highlightEnabled() ? m_selection->objectUUID() : std::nullopt;
}
