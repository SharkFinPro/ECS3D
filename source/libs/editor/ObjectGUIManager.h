#ifndef OBJECTGUIMANAGER_H
#define OBJECTGUIMANAGER_H

#include <functional>
#include <memory>
#include <optional>
#include <uuid.h>

class ObjectManager;
class Object;
class Component;
class ComponentEditor;

// The editor's object tree ("Objects") + selected-object panel ("Selected Object"). It draws each
// component of the selected object through the ComponentEditor and, when one is edited, fires the edit
// callback so the EditorApp can send the change to the authoritative server.
class ObjectGUIManager {
public:
  using EditCallback = std::function<void(const uuids::uuid& objectUUID, const std::shared_ptr<Component>& component)>;

  explicit ObjectGUIManager(std::shared_ptr<ComponentEditor> componentEditor);

  void setEditCallback(EditCallback callback);

  void displayGui(ObjectManager& objectManager);

private:
  std::shared_ptr<ComponentEditor> m_componentEditor;

  EditCallback m_editCallback;

  std::optional<uuids::uuid> m_selectedObject;

  void displayObjectTree(const std::shared_ptr<Object>& object);

  void displaySelectedObject(ObjectManager& objectManager);

  void displayComponent(const uuids::uuid& objectUUID, const std::shared_ptr<Component>& component);
};



#endif //OBJECTGUIMANAGER_H
