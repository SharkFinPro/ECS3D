#include "InspectorPanel.h"
#include "AssetInspector.h"
#include "GuiComponents.h"
#include "ObjectInspector.h"
#include "Selection.h"
#include <assets/AssetRegistry.h>
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <imgui.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

InspectorPanel::InspectorPanel(std::shared_ptr<ComponentEditor> componentEditor,
                               std::shared_ptr<ComponentRegistry> componentRegistry,
                               std::shared_ptr<GpuAssetCache> assetCache)
  // componentEditor is shared by both inspectors (the asset inspector reuses the object editors for the
  // editable prefab body). m_objectInspector is initialised first (declaration order), so it takes a copy
  // and the asset inspector moves the last reference.
  : m_objectInspector(std::make_unique<ObjectInspector>(componentEditor)),
    m_assetInspector(std::make_unique<AssetInspector>(std::move(assetCache), std::move(componentRegistry),
                                                      std::move(componentEditor)))
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
  m_assetInspector->setAssetRegistry(registry);
}

void InspectorPanel::setEditable(const bool editable)
{
  m_objectInspector->setEditable(editable);
  m_assetInspector->setEditable(editable);
}

void InspectorPanel::setEditCallback(EditCallback callback)
{
  m_objectInspector->setEditCallback(std::move(callback));
}

void InspectorPanel::setEditCommittedCallback(EditCommittedCallback callback)
{
  m_objectInspector->setEditCommittedCallback(std::move(callback));
}

void InspectorPanel::setSceneEditCallback(SceneEditCallback callback)
{
  m_objectInspector->setSceneEditCallback(std::move(callback));
}

void InspectorPanel::setLoadSceneCallback(std::function<void(const uuids::uuid& sceneUUID)> callback)
{
  m_assetInspector->setLoadSceneCallback(std::move(callback));
}

void InspectorPanel::setRenameAssetCallback(std::function<void(const uuids::uuid& assetUUID, const std::string& displayName)> callback)
{
  m_assetInspector->setRenameCallback(std::move(callback));
}

void InspectorPanel::setRemoveAssetCallback(std::function<void(const uuids::uuid& assetUUID)> callback)
{
  m_assetInspector->setRemoveCallback(std::move(callback));
}

void InspectorPanel::setAssetReferenceCountCallback(std::function<int(const uuids::uuid& assetUUID)> callback)
{
  m_assetInspector->setReferenceCountCallback(std::move(callback));
}

void InspectorPanel::setUpdatePrefabBodyCallback(std::function<void(const uuids::uuid& assetUUID, const std::string& name, const std::string& body)> callback)
{
  m_assetInspector->setUpdatePrefabBodyCallback(std::move(callback));
}

void InspectorPanel::displayGui(const ObjectManager* objectManager, const std::optional<uuids::uuid>& activeSceneUUID)
{
  ImGui::Begin("Inspector");

  // Drop a stale asset selection when the registry changed under us and the uuid is gone (an asset
  // deletion, or a fresh snapshot). Gated on the registry version so the membership re-check only
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

  // A multi-object selection: show/edit the components common to every selected object rather than only
  // the primary one (see ObjectInspector::displayMulti).
  if (m_selection->kind() == EditorSelection::Kind::Object && m_selection->size() > 1)
  {
    std::vector<std::shared_ptr<Object>> objects;
    if (objectManager)
    {
      for (const auto& uuid : m_selection->items())
      {
        if (auto object = objectManager->getObjectByUUID(uuid))
        {
          objects.push_back(std::move(object));
        }
      }
    }

    // Every selected uuid but one went stale (e.g. a fresh snapshot dropped them): show the one that's
    // left the same way a plain single-object selection would, rather than an empty state a selection
    // that still names a live object shouldn't reach.
    if (objects.size() == 1)
    {
      // Flushes a multi-selection gesture that may still be in flight from a prior frame (the rest of the
      // selection just dropped out from under it) before switching to the single-object display below.
      m_objectInspector->commitPendingEdit();

      gc::sectionLabel("Inspector");
      m_objectInspector->displayTypeChip(objects.front());
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      m_objectInspector->display(objects.front());

      ImGui::End();
      return;
    }

    gc::sectionLabel("Inspector");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (objects.empty())
    {
      // Nothing in the selection resolved to a live object at all: finish whatever gesture was in
      // flight rather than leave it to attach to what's next.
      m_objectInspector->commitPendingEdit();
      gc::emptyState(gc::SecIcon::block, "Nothing selected", "Select an object or asset to inspect it");
    }
    else
    {
      m_objectInspector->displayMulti(objects);
    }

    ImGui::End();
    return;
  }

  // Resolve the current selection to a concrete inspector. A None kind (or a stale one cleared above)
  // leaves both null and falls through to the empty state.
  const auto selectedObjectUUID = m_selection->objectUUID();
  const auto object = (objectManager && selectedObjectUUID.has_value())
    ? objectManager->getObjectByUUID(selectedObjectUUID.value()) : nullptr;

  const auto selectedAssetUUID = m_selection->assetUUID();
  const AssetRecord* asset = (m_assetRegistry && selectedAssetUUID.has_value())
    ? m_assetRegistry->getByUUID(selectedAssetUUID.value()) : nullptr;

  // Every path below that is not the object inspector skips its own end-of-frame commit, including the
  // empty state's early return - so a gesture interrupted by the object vanishing or the selection
  // moving to an asset is finished here rather than left to attach itself to the next object.
  if (!object)
  {
    m_objectInspector->commitPendingEdit();
  }

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

  // Empty state when nothing renderable is selected - an intentional placeholder rather than a lone
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
    m_assetInspector->display(*asset, activeSceneUUID);
  }

  ImGui::End();
}

std::span<const uuids::uuid> InspectorPanel::getHighlightUUIDs() const
{
  if (!m_objectInspector->highlightEnabled() || m_selection->kind() != EditorSelection::Kind::Object)
  {
    return {};
  }

  return m_selection->items();
}
