#ifndef OBJECTGUIMANAGER_H
#define OBJECTGUIMANAGER_H

#include <nlohmann/json_fwd.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>
#include <uuid.h>

class ObjectManager;
class Object;
class Component;
class ComponentEditor;

// The editor's object tree ("Objects") + selected-object panel ("Selected Object"). It draws each
// component of the selected object through the ComponentEditor and reports edits back:
//   - a component VALUE change -> EditCallback(objectUUID, component)
//   - a STRUCTURAL change (add/remove object or component) -> SceneEditCallback(<built edit json>)
// The EditorApp turns both into network messages for the authoritative server.
class ObjectGUIManager {
public:
  using EditCallback = std::function<void(const uuids::uuid& objectUUID, const std::shared_ptr<Component>& component)>;
  using SceneEditCallback = std::function<void(const nlohmann::json& edit)>;

  explicit ObjectGUIManager(std::shared_ptr<ComponentEditor> componentEditor);

  void setEditCallback(EditCallback callback);

  void setSceneEditCallback(SceneEditCallback callback);

  // objectManager may be null (no scene loaded yet): the windows are still drawn, just empty, so they
  // stay present/dockable instead of popping in and out.
  void displayGui(ObjectManager* objectManager);

  // Set the selection externally (e.g. from viewport mouse-picking).
  void setSelectedObject(const std::optional<uuids::uuid>& objectUUID);

  // The object to highlight in the viewport: the selection when "Highlight Object" is on, else nullopt.
  [[nodiscard]] std::optional<uuids::uuid> getHighlightUUID() const;

private:
  std::shared_ptr<ComponentEditor> m_componentEditor;

  EditCallback m_editCallback;
  SceneEditCallback m_sceneEditCallback;

  std::optional<uuids::uuid> m_selectedObject;

  bool m_highlightSelectedObject = true;

  // Components whose deletion we've already sent (markedAsDeleted persists until the next snapshot
  // rebuilds the object), so we don't re-send the same removeComponent every frame.
  std::unordered_set<const Component*> m_pendingRemovals;

  void displayObjectTree(const std::shared_ptr<Object>& object);

  void displaySelectedObject(ObjectManager* objectManager);

  void displayAddComponent(const std::shared_ptr<Object>& object);

  void displayComponent(const uuids::uuid& objectUUID, const std::shared_ptr<Component>& component);
};



#endif //OBJECTGUIMANAGER_H
