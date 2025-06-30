#ifndef OBJECTGUIMANAGER_H
#define OBJECTGUIMANAGER_H

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
  ~ObjectGUIManager() = default;

  void update();

  void addObject(const std::shared_ptr<Object>& object, const std::shared_ptr<ObjectUINode>& parentUINode = nullptr);

  void displaySelectedObjectGui() const;

private:
  ObjectManager* objectManager;

  std::vector<std::shared_ptr<ObjectUINode>> objectUINodes;
  std::vector<std::shared_ptr<ObjectUINode>> objectUINodesSetForReassignment;

  std::shared_ptr<Object> selectedObject;

  static bool containsObjectUINode(const std::vector<std::shared_ptr<ObjectUINode>>& rootNodes,
                                   const std::shared_ptr<Object>& object);

  static bool isAncestor(const std::shared_ptr<ObjectUINode>& source, const std::shared_ptr<ObjectUINode>& target);

  void reorderObjectGui();

  void displayObjectDragDrop(const std::shared_ptr<ObjectUINode>& node);

  void displayCreateObjectChildButton(const std::shared_ptr<ObjectUINode>& node);

  void displayObjectGui(const std::shared_ptr<ObjectUINode>& node);

  void displayObjectListGui();
};



#endif //OBJECTGUIMANAGER_H
