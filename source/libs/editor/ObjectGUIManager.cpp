#include "ObjectGUIManager.h"
#include "AssetDragDrop.h"
#include "GuiComponents.h"
#include "Selection.h"
#include <Replication.h>
#include <SettingsStore.h>
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {
  constexpr const char* sortModeKey = "panels.objects.sortMode";

  ObjectGUIManager::SortMode parseSortMode(const std::string& value)
  {
    return value == "alphabetical" ? ObjectGUIManager::SortMode::alphabetical : ObjectGUIManager::SortMode::authored;
  }

  const char* sortModeToString(const ObjectGUIManager::SortMode mode)
  {
    return mode == ObjectGUIManager::SortMode::alphabetical ? "alphabetical" : "authored";
  }

  // ASCII-only case fold, same approach AssetBrowserPanel uses for its own name sort/search - no
  // std::locale, so this never depends on the environment.
  [[nodiscard]] bool ciNameLess(const std::string& a, const std::string& b)
  {
    return std::ranges::lexicographical_compare(a, b, [](const char x, const char y) {
      return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
    });
  }

  // The display order for `objects`, never the scene's own order: authored order is `objects` itself
  // (today that's ObjectManager/Object's own load order, since there is no persisted sibling order yet),
  // untouched and unsorted - so the caller-owned `scratch` is only filled and sorted (a stable sort, so
  // two same-named objects keep their authored relative order) when alphabetical mode actually needs a
  // reordered copy. `scratch` must outlive the reference this returns, so it lives in the caller's own
  // loop scope rather than inside this function.
  [[nodiscard]] const std::vector<std::shared_ptr<Object>>& sortedForDisplay(
    const std::vector<std::shared_ptr<Object>>& objects, const ObjectGUIManager::SortMode mode,
    std::vector<std::shared_ptr<Object>>& scratch)
  {
    if (mode != ObjectGUIManager::SortMode::alphabetical)
    {
      return objects;
    }

    scratch = objects;
    std::ranges::stable_sort(scratch, [](const std::shared_ptr<Object>& a, const std::shared_ptr<Object>& b) {
      return ciNameLess(a->getName(), b->getName());
    });
    return scratch;
  }

  // The panel's uuid source, seeded the way ObjectManager seeds its own: a single random_device word
  // would leave mt19937 with 2^32 possible streams, and a toolchain whose random_device is deterministic
  // would hand every run of the editor the same sequence - so the first object created in one run would
  // carry the uuid the first object of the previous run already has, and the server would refuse it.
  // Held across calls rather than reseeded per uuid; only the UI thread reaches this.
  [[nodiscard]] uuids::uuid newUUID()
  {
    static std::mt19937 rng = [] {
      std::random_device rd;
      auto seedData = std::array<int, std::mt19937::state_size>{};
      std::ranges::generate(seedData, std::ref(rd));
      std::seed_seq seq(seedData.begin(), seedData.end());
      return std::mt19937(seq);
    }();
    static uuids::uuid_random_generator generator{ rng };

    return generator();
  }

  // A fresh asset uuid for a saved prefab. (AssetBrowserPanel has its own for the assets it creates;
  // asset uuids are unrelated to the scene's object uuids, so ObjectManager's generator is not the right
  // source here.)
  [[nodiscard]] std::string newAssetUUID()
  {
    return uuids::to_string(newUUID());
  }

  // The uuid this panel asks the server to give an object it is creating, so the edit it sends names the
  // object it produces rather than one only the server knows about. The panel is only ever handed a const
  // ObjectManager, so this generates its own instead of borrowing the manager's generator; the server
  // refuses a uuid already in use either way.
  [[nodiscard]] uuids::uuid newObjectUUID()
  {
    return newUUID();
  }

  // Heuristic icon for an object derived from its components (the mockup shows a per-object glyph). The
  // Inspector's ObjectInspector keeps its own copy of this mapping for the selected-object header chip.
  gc::SecIcon iconForObject(const std::shared_ptr<Object>& object)
  {
    const auto& components = object->getComponents();

    if (components.contains(ComponentType::lightRenderer))
    {
      return gc::SecIcon::light;
    }

    if (const auto collider = components.find(ComponentType::collider); collider != components.end())
    {
      if (collider->second->getSubType() == ComponentType::SubComponentType_sphereCollider)
      {
        return gc::SecIcon::sphere;
      }
      return components.contains(ComponentType::rigidBody) ? gc::SecIcon::rigidblock : gc::SecIcon::block;
    }

    if (components.contains(ComponentType::modelRenderer))
    {
      return gc::SecIcon::model;
    }

    return gc::SecIcon::block;
  }

  // The object being dragged in the hierarchy, or null when no object drag is in flight. Resolved once
  // per frame so every row can test the drop against the engine's own cycle predicate.
  [[nodiscard]] std::shared_ptr<Object> draggedObject(const ObjectManager* objectManager)
  {
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();

    if (!objectManager || !payload || !payload->IsDataType("object"))
    {
      return nullptr;
    }

    const std::string uuidStr(static_cast<const char*>(payload->Data), payload->DataSize);
    const auto dragged = uuids::uuid::from_string(uuidStr);

    return dragged.has_value() ? objectManager->getObjectByUUID(dragged.value()) : nullptr;
  }

  // Shift-click range: the contiguous slice of `visibleOrder` (the tree's current on-screen order) from
  // `anchor` to `clicked`, inclusive and ordered anchor-to-clicked so the caller can add it to the
  // selection with `clicked` landing last (the new primary). Either uuid missing from `visibleOrder` (a
  // stale anchor, or a row hidden behind a collapsed ancestor) falls back to just `clicked`.
  [[nodiscard]] std::vector<uuids::uuid> rangeBetween(const std::vector<uuids::uuid>& visibleOrder,
                                                       const uuids::uuid& anchor, const uuids::uuid& clicked)
  {
    const auto anchorIt = std::ranges::find(visibleOrder, anchor);
    const auto clickedIt = std::ranges::find(visibleOrder, clicked);
    if (anchorIt == visibleOrder.end() || clickedIt == visibleOrder.end())
    {
      return { clicked };
    }

    std::vector<uuids::uuid> range;
    if (anchorIt <= clickedIt)
    {
      range.assign(anchorIt, clickedIt + 1);
    }
    else
    {
      range.assign(clickedIt, anchorIt + 1);
      std::ranges::reverse(range);
    }
    return range;
  }
}

void ObjectGUIManager::setSceneEditCallback(SceneEditCallback callback)
{
  m_sceneEditCallback = std::move(callback);
}

void ObjectGUIManager::setAddAssetCallback(AddAssetCallback callback)
{
  m_addAssetCallback = std::move(callback);
}

void ObjectGUIManager::setSelection(std::shared_ptr<EditorSelection> selection)
{
  m_selection = std::move(selection);
}

void ObjectGUIManager::setEditable(const bool editable)
{
  m_editable = editable;
}

void ObjectGUIManager::setSettings(SettingsStore* settings)
{
  m_settings = settings;

  if (m_settings)
  {
    m_sortMode = parseSortMode(m_settings->get<std::string>(sortModeKey, "authored"));
  }
}

ObjectGUIManager::SortMode ObjectGUIManager::sortMode() const
{
  return m_sortMode;
}

void ObjectGUIManager::setSortMode(const SortMode mode)
{
  if (m_sortMode == mode)
  {
    return;
  }

  m_sortMode = mode;

  if (m_settings)
  {
    m_settings->set(sortModeKey, std::string(sortModeToString(mode)));
  }
}

void ObjectGUIManager::displayGui(const ObjectManager* objectManager)
{
  ImGui::Begin("Objects");

  // A fresh snapshot rebuilds every Object behind a new shared_ptr; the selection only keeps uuids
  // (never a pointer), so this drops the ones that no longer resolve to anything rather than leaving
  // them to linger as a selection nothing on screen matches.
  if (objectManager && m_selection->kind() == EditorSelection::Kind::Object)
  {
    m_selection->pruneMissing([objectManager](const uuids::uuid& id) {
      return objectManager->getObjectByUUID(id) != nullptr;
    });
  }

  if (!m_editable)
  {
    ImGui::TextColored(theme::scriptAmber, "Read-only - server is not in edit mode");
    ImGui::Separator();
  }

  // Section header with a right-aligned count pill (mockup: "OBJECTS  [10]").
  if (objectManager)
  {
    gc::sectionLabel("Objects");

    // Every object in the scene, not just the roots - matching the count the apps log on a snapshot.
    const auto count = std::to_string(objectManager->getAllObjects().size());
    const float pillWidth = ImGui::CalcTextSize(count.c_str()).x + 18.0f;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - pillWidth);
    gc::pill(count.c_str(), theme::t3);

    ImGui::Spacing();

    displaySortControl();

    ImGui::Spacing();
  }

  ImGui::BeginDisabled(!m_editable || objectManager == nullptr);
  if (gc::accentButton("Create New Object", gc::SecIcon::plus))
  {
    const auto created = newObjectUUID();
    m_sceneEditCallback(replication::buildAddObject("Object", nullptr, &created));
  }
  ImGui::EndDisabled();

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (objectManager)
  {
    m_dragSource = draggedObject(objectManager);
    m_visibleOrder.clear();

    std::vector<std::shared_ptr<Object>> sortedRootsScratch;
    for (const auto& object : sortedForDisplay(objectManager->getObjects(), m_sortMode, sortedRootsScratch))
    {
      displayObjectTree(object);
    }

    // A Shift-click range needs the full on-screen order, which isn't known until every row above has
    // been visited - so the click a row registered this frame is only resolved now.
    applyPendingClick();

    // The empty area below the tree is the scene root: drop an object there to reparent it to the root,
    // or a prefab from the asset browser to instantiate it.
    ImGui::Dummy(ImGui::GetContentRegionAvail());
    if (m_editable && ImGui::BeginDragDropTarget())
    {
      if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("object"))
      {
        const std::string uuidStr(static_cast<const char*>(payload->Data), payload->DataSize);
        if (const auto dragged = uuids::uuid::from_string(uuidStr); dragged.has_value() && m_sceneEditCallback)
        {
          m_sceneEditCallback(replication::buildReparentObject(dragged.value(), nullptr));
        }
      }

      if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetDragDrop::prefab))
      {
        const std::string uuidStr(static_cast<const char*>(payload->Data), payload->DataSize);
        if (const auto prefab = uuids::uuid::from_string(uuidStr); prefab.has_value() && m_sceneEditCallback)
        {
          // The server resolves the prefab uuid to its body on disk, instantiates, and re-snapshots.
          const auto instance = newObjectUUID();
          m_sceneEditCallback(replication::buildInstantiatePrefab(prefab.value(), nullptr, &instance));
        }
      }

      ImGui::EndDragDropTarget();
    }
  }

  // Delete hotkey: while the Objects panel has focus, Delete queues the selected object for removal
  // (guarded against firing while a text field is being typed into, or when read-only). The
  // confirmation modal follows.
  if (m_editable && objectManager && m_selection->objectUUID().has_value() &&
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      !ImGui::GetIO().WantTextInput &&
      ImGui::IsKeyPressed(ImGuiKey_Delete))
  {
    m_objectPendingDeletion = m_selection->objectUUID();
  }

  ImGui::End();

  m_dragSource.reset();

  displayDeleteConfirmationModal(objectManager);
}

bool ObjectGUIManager::canAcceptObjectDrop(const std::shared_ptr<Object>& target) const
{
  // A reparent onto the dragged object itself or onto one of its own descendants would cycle the graph,
  // so the row refuses the drop instead of sending an edit the server rejects anyway.
  return !m_dragSource || (m_dragSource != target && !m_dragSource->isAncestorOf(target));
}

void ObjectGUIManager::displaySortControl()
{
  ImGui::TextColored(theme::t3, "Sort");
  ImGui::SameLine();

  const char* label = m_sortMode == SortMode::alphabetical ? "A-Z" : "Authored";
  if (ImGui::SmallButton(label))
  {
    ImGui::OpenPopup("ObjectsSortMode");
  }

  if (ImGui::BeginPopup("ObjectsSortMode"))
  {
    if (ImGui::MenuItem("Authored Order", nullptr, m_sortMode == SortMode::authored))
    {
      setSortMode(SortMode::authored);
    }

    if (ImGui::MenuItem("Alphabetical", nullptr, m_sortMode == SortMode::alphabetical))
    {
      setSortMode(SortMode::alphabetical);
    }

    ImGui::EndPopup();
  }
}

void ObjectGUIManager::displayObjectTree(const std::shared_ptr<Object>& object)
{
  ImGui::PushID(uuids::to_string(object->getUUID()).c_str());

  // Recorded in display order regardless of selection - this row is on screen either way, and a
  // Shift-click range is measured against exactly this list (see m_visibleOrder).
  m_visibleOrder.push_back(object->getUUID());

  const bool isSelected = m_selection->kind() == EditorSelection::Kind::Object &&
                           m_selection->contains(object->getUUID());
  const bool isLeaf = object->getChildren().empty();

  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_FramePadding |
                             ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap |
                             ImGuiTreeNodeFlags_OpenOnDoubleClick;
  if (isSelected)
  {
    flags |= ImGuiTreeNodeFlags_Selected;
  }
  if (isLeaf)
  {
    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  }

  // Empty label: we draw the icon + name ourselves so the row matches the mockup (per-type glyph + an
  // accent left-bar on the selected row). Accent-tint the node's selection/hover fills.
  ImGui::PushStyleColor(ImGuiCol_Header, theme::accdim);
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, theme::hover);
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, theme::accdim);
  // Taller rows than the default frame padding so the row + its inline buttons have more breathing room.
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 11.0f));
  const bool open = ImGui::TreeNodeEx("##node", flags);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(3);

  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
  {
    // Deferred to applyPendingClick(), once the full display order for this frame is known (a
    // Shift-click range needs it). The Inspector's ObjectInspector folds its Add Component list closed
    // on its own when it notices the selection changed, so nothing else is needed here.
    const auto& io = ImGui::GetIO();
    m_pendingClick = PendingClick{ object->getUUID(), io.KeyCtrl, io.KeyShift };
  }

  // Icon + name + accent selection bar, drawn over the (empty-label) node row.
  const float rowHeight = ImGui::GetItemRectMax().y - ImGui::GetItemRectMin().y;
  {
    const ImVec2 rmin = ImGui::GetItemRectMin();
    const ImVec2 rmax = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (isSelected)
    {
      dl->AddRectFilled(rmin, ImVec2(rmin.x + 2.5f, rmax.y), theme::u32(theme::accent));
    }

    const float tx = rmin.x + ImGui::GetTreeNodeToLabelSpacing();
    const float cy = (rmin.y + rmax.y) * 0.5f;
    gc::drawSecIcon(dl, ImVec2(tx + 8.0f, cy), 16.0f, iconForObject(object), theme::u32(theme::t2));

    const float textX = tx + 22.0f;
    const float nameMax = rmax.x - textX - 72.0f;
    const std::string shown = gc::ellipsize(object->getName().c_str(), nameMax);
    dl->AddText(ImVec2(textX, cy - ImGui::GetTextLineHeight() * 0.5f), theme::u32(theme::t1), shown.c_str());
  }

  // Drag this node onto another to reparent it (drop on empty space in the window reparents to root).
  // Reparenting is a mutation, so it's only available when the server is editable.
  if (m_editable && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
  {
    const std::string uuidStr = uuids::to_string(object->getUUID());
    ImGui::SetDragDropPayload("object", uuidStr.c_str(), uuidStr.size());
    ImGui::TextUnformatted(object->getName().c_str());
    ImGui::EndDragDropSource();
  }

  if (m_editable && canAcceptObjectDrop(object) && ImGui::BeginDragDropTarget())
  {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("object"))
    {
      const std::string uuidStr(static_cast<const char*>(payload->Data), payload->DataSize);
      if (const auto dragged = uuids::uuid::from_string(uuidStr); dragged.has_value() && m_sceneEditCallback)
      {
        const auto parent = object->getUUID();
        m_sceneEditCallback(replication::buildReparentObject(dragged.value(), &parent));
      }
    }

    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetDragDrop::prefab))
    {
      const std::string uuidStr(static_cast<const char*>(payload->Data), payload->DataSize);
      if (const auto prefab = uuids::uuid::from_string(uuidStr); prefab.has_value() && m_sceneEditCallback)
      {
        // Same op as the empty-space drop, but with this row as the parent instead of the scene root.
        const auto parent = object->getUUID();
        const auto instance = newObjectUUID();
        m_sceneEditCallback(replication::buildInstantiatePrefab(prefab.value(), &parent, &instance));
      }
    }

    ImGui::EndDragDropTarget();
  }

  // The context menu is mutation-only (add child / duplicate / delete), so it's suppressed when the
  // server is read-only.
  if (m_editable && ImGui::BeginPopupContextItem())
  {
    if (ImGui::MenuItem("Add Child") && m_sceneEditCallback)
    {
      const auto parent = object->getUUID();
      const auto created = newObjectUUID();
      m_sceneEditCallback(replication::buildAddObject("Object", &parent, &created));
    }

    if (ImGui::MenuItem("Duplicate") && m_sceneEditCallback)
    {
      const auto duplicate = newObjectUUID();
      m_sceneEditCallback(replication::buildDuplicateObject(object->getUUID(), &duplicate));
    }

    if (ImGui::MenuItem("Save as Prefab"))
    {
      saveAsPrefab(object);
    }

    if (ImGui::MenuItem("Delete"))
    {
      // Queue the object; displayDeleteConfirmationModal() prompts before actually removing it.
      m_objectPendingDeletion = object->getUUID();
    }

    ImGui::EndPopup();
  }

  // Add/Delete Buttons, flush to the right edge of the row.
  ImGui::SameLine();

  const float buttonWidth = rowHeight;
  constexpr float buttonGap = 3.0f;
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - buttonWidth * 2.0f - buttonGap);

  ImGui::SetNextItemAllowOverlap();
  if (gc::rowIconButton("addChild", gc::SecIcon::plus, false, buttonWidth, rowHeight))
  {
    const auto parent = object->getUUID();
    const auto created = newObjectUUID();
    m_sceneEditCallback(replication::buildAddObject("Object", &parent, &created));
  }

  ImGui::SameLine(0.0f, buttonGap);

  ImGui::SetNextItemAllowOverlap();
  if (gc::rowIconButton("deleteObject", gc::SecIcon::minus, true, buttonWidth, rowHeight))
  {
    // Queue the object; displayDeleteConfirmationModal() prompts before actually removing it.
    m_objectPendingDeletion = object->getUUID();
  }

  if (open && !isLeaf)
  {
    std::vector<std::shared_ptr<Object>> sortedChildrenScratch;
    for (const auto& child : sortedForDisplay(object->getChildren(), m_sortMode, sortedChildrenScratch))
    {
      displayObjectTree(child);
    }

    ImGui::TreePop();
  }

  ImGui::PopID();
}

void ObjectGUIManager::applyPendingClick()
{
  if (!m_pendingClick.has_value())
  {
    return;
  }

  const auto [uuid, ctrl, shift] = m_pendingClick.value();
  m_pendingClick.reset();

  // Shift-click (plain or Ctrl+Shift): select/add the contiguous range from the anchor to this row, in
  // the tree's current display order. The anchor itself doesn't move, so repeated Shift-clicks keep
  // extending from the same start.
  if (shift && m_rangeAnchor.has_value())
  {
    const auto range = rangeBetween(m_visibleOrder, m_rangeAnchor.value(), uuid);
    if (!ctrl)
    {
      m_selection->clear();
    }
    for (const auto& id : range)
    {
      m_selection->addObject(id);
    }

    // The anchor wasn't on screen (deleted, or hidden behind a collapsed ancestor) - rangeBetween fell
    // back to just this row, so start the next range from here instead of repeating the same fallback.
    if (range.size() == 1)
    {
      m_rangeAnchor = uuid;
    }
    return;
  }

  if (ctrl)
  {
    m_selection->toggleObject(uuid);
  }
  else
  {
    m_selection->selectObject(uuid);
  }
  m_rangeAnchor = uuid;
}

void ObjectGUIManager::displayDeleteConfirmationModal(const ObjectManager* objectManager)
{
  if (!m_objectPendingDeletion.has_value())
  {
    return;
  }

  // The pending object can disappear out from under us (a fresh snapshot, or another editor removing
  // it): drop the prompt rather than confirming a stale delete.
  const auto object = objectManager ? objectManager->getObjectByUUID(m_objectPendingDeletion.value()) : nullptr;
  if (!object)
  {
    m_objectPendingDeletion.reset();
    return;
  }

  ImGui::OpenPopup("Delete Object?");

  bool shouldDelete = false;

  if (ImGui::BeginPopupModal("Delete Object?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::TextUnformatted("Are you sure you want to delete");
    ImGui::SameLine();
    ImGui::TextColored(theme::accent, "%s", object->getName().c_str());
    ImGui::SameLine();
    ImGui::TextUnformatted("?");

    // Children survive a delete (ObjectManager::deleteObjectsMarkedForDeletion reparents them to the
    // deleted object's own parent, or the scene root) - say so, since that's easy to miss.
    if (const auto& children = object->getChildren(); !children.empty())
    {
      const auto parent = object->getParent();
      const std::string destination = parent ? parent->getName() : "the scene root";
      ImGui::TextColored(theme::scriptAmber, "Its %zu %s will be kept and moved to %s.", children.size(),
                         children.size() == 1 ? "child" : "children", destination.c_str());
    }

    ImGui::TextColored(theme::t3, "This action cannot be undone.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Danger-red confirm; neutral cancel.
    ImGui::PushStyleColor(ImGuiCol_Button, theme::danger);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::v4(240, 110, 114));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::v4(210, 70, 75));
    ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(255, 255, 255));
    if (ImGui::Button("Delete", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter))
    {
      shouldDelete = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor(4);

    ImGui::SameLine();

    if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
      m_objectPendingDeletion.reset();
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

  if (shouldDelete)
  {
    if (m_sceneEditCallback)
    {
      m_sceneEditCallback(replication::buildRemoveObject(m_objectPendingDeletion.value()));
    }

    // Drop just this uuid rather than the whole selection - the deleted object may have been one of
    // several selected, and the rest still exist.
    if (m_selection->kind() == EditorSelection::Kind::Object && m_selection->contains(m_objectPendingDeletion.value()))
    {
      m_selection->remove(m_objectPendingDeletion.value());
    }

    m_objectPendingDeletion.reset();
  }
}

void ObjectGUIManager::saveAsPrefab(const std::shared_ptr<Object>& object) const
{
  if (!m_addAssetCallback)
  {
    return;
  }

  // The prefab body is the object's own serialize() blob, carried inline in the asset record - there is no
  // file on disk, so it travels with the project and reaches a server that shares no filesystem with us.
  //
  // Prefabs are keyed by display name: saving over an existing name updates that prefab's body in place
  // (keeping its uuid) rather than adding a second one. The uuid here is only used when the name is new.
  m_addAssetCallback({
    { "assetType", "prefab" },
    { "uuid", newAssetUUID() },
    { "name", object->getName() },
    { "body", object->serialize().dump() }
  });
}
