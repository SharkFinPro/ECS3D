#ifndef OBJECTGUIMANAGER_H
#define OBJECTGUIMANAGER_H

#include <VulkanEngine/utilities/EventSystem.h>
#include <VulkanEngine/components/window/Window.h>
#include <vector>
#include <memory>

class ObjectManager;
class Object;

struct ObjectUINode {
  std::shared_ptr<Object> object;
  std::shared_ptr<ObjectUINode> parent = nullptr;
  std::vector<std::shared_ptr<ObjectUINode>> children;
  std::shared_ptr<ObjectUINode> newParent = nullptr;
};

class ObjectGUIManager {
public:
  explicit ObjectGUIManager(ObjectManager* objectManager);

  ~ObjectGUIManager();

  void update();

  void addObject(const std::shared_ptr<Object>& object, const std::shared_ptr<ObjectUINode>& parentUINode = nullptr);

  void displaySelectedObjectGui();

private:
  ObjectManager* m_objectManager;

  std::vector<std::shared_ptr<ObjectUINode>> m_objectUINodes;
  std::vector<std::shared_ptr<ObjectUINode>> m_pendingReassignments;
  std::vector<std::shared_ptr<ObjectUINode>> m_pendingDeletions;

  std::shared_ptr<Object> m_selectedObject;

  std::shared_ptr<ObjectUINode> m_focusedNode;

  std::shared_ptr<ObjectUINode> m_nodeCheckingForDeletion;

  bool m_highlightSelectedObject = true;

  bool m_mouseWasPressed = false;

  vke::EventListener<vke::KeyCallbackEvent> m_keyCallbackEventListener;

  static bool containsObjectUINode(const std::vector<std::shared_ptr<ObjectUINode>>& rootNodes,
                                   const std::shared_ptr<Object>& object);

  static bool isAncestor(const std::shared_ptr<ObjectUINode>& source, const std::shared_ptr<ObjectUINode>& target);

  void processReassignments();

  void processDeletions();

  void displayObjectDragDrop(const std::shared_ptr<ObjectUINode>& node);

  void displayCreateObjectChildButton(const std::shared_ptr<ObjectUINode>& node);

  void displayDeleteObjectButton(const std::shared_ptr<ObjectUINode>& node);

  void displayObjectGui(const std::shared_ptr<ObjectUINode>& node);

  void displayObjectListGui();

  void registerWindowEvents();

  void displayDeleteConfirmationModal();

  void deleteNodeQueriedForDeletion();
};



#endif //OBJECTGUIMANAGER_H
