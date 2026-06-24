#ifndef BINDINGCONTEXT_H
#define BINDINGCONTEXT_H

class ObjectManager;

// Replaces the old static ECS3D* s_ecs the bindings used. ScriptSystem points this at the server's
// current ObjectManager each tick so the (static, C-ABI) bindings can resolve a uuid to its object.
class BindingContext {
public:
  static void setObjectManager(ObjectManager* objectManager);

  [[nodiscard]] static ObjectManager* getObjectManager();

private:
  static ObjectManager* s_objectManager;
};



#endif //BINDINGCONTEXT_H
