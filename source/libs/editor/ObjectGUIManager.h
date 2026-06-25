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

  void displayGui(ObjectManager& objectManager);

private:
  std::shared_ptr<ComponentEditor> m_componentEditor;

  EditCallback m_editCallback;
  SceneEditCallback m_sceneEditCallback;

  std::optional<uuids::uuid> m_selectedObject;

  // Components whose deletion we've already sent (markedAsDeleted persists until the next snapshot
  // rebuilds the object), so we don't re-send the same removeComponent every frame.
  std::unordered_set<const Component*> m_pendingRemovals;

  void displayObjectTree(const std::shared_ptr<Object>& object);

  void displaySelectedObject(ObjectManager& objectManager);

  void displayAddComponent(const std::shared_ptr<Object>& object);

  void displayComponent(const uuids::uuid& objectUUID, const std::shared_ptr<Component>& component);
};



#endif //OBJECTGUIMANAGER_H
