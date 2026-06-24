#ifndef OBJECTGUIMANAGER_H
#define OBJECTGUIMANAGER_H

#include <VulkanEngine/utilities/EventSystem.h>
#include <VulkanEngine/components/window/Window.h>
#include <vector>
#include <memory>

class ObjectManager;
class Object;

class ObjectGUIManager {
public:
  explicit ObjectGUIManager(ObjectManager* objectManager);

  ~ObjectGUIManager();

  void update();

  void displaySelectedObjectGui();

private:
  ObjectManager* m_objectManager;

  std::shared_ptr<Object> m_selectedObject;

  std::shared_ptr<Object> m_focusedObject;

  std::shared_ptr<Object> m_objectCheckingForDeletion;

  struct ReassignmentData {
    std::shared_ptr<Object> object;
    std::shared_ptr<Object> newParent;
  };
  ReassignmentData m_pendingReassignment;

  bool m_highlightSelectedObject = true;

  bool m_mouseWasPressed = false;

  vke::EventListener<vke::KeyCallbackEvent> m_keyCallbackEventListener;

  void displayObjectDragDrop(const std::shared_ptr<Object>& object);

  void displayCreateObjectChildButton(const std::shared_ptr<Object>& object);

  void displayDeleteObjectButton(const std::shared_ptr<Object>& object);

  void displayObjectGui(const std::shared_ptr<Object>& object);

  void displayObjectListGui();

  void registerWindowEvents();

  void displayDeleteConfirmationModal();

  void deleteNodeQueriedForDeletion();

  void processReassignment();
};



#endif //OBJECTGUIMANAGER_H
