#ifndef COMPONENTOPSBINDINGS_H
#define COMPONENTOPSBINDINGS_H

// Generic add/remove/query component operations exposed to scripts, keyed by the ComponentRegistry's
// type-name strings (the same names Object::loadFromJSON/ComponentRegistry::create use - "RigidBody",
// "Box", "Sphere", ... - see componentTypeToRegistryKey in Component.h) rather than a native enum, since
// ComponentType's packed value is a wire discriminator the managed side must not derive independently.
//
// Transform is refused by addComponent/removeComponent (it is structural - every other system assumes an
// object has one) but still shows up in hasComponent/getComponentTypes, since querying it is harmless and
// it is a real component the object owns. Script is excluded everywhere (adding one needs a class name
// this API does not take, a script adds another script through a separate, not-yet-built call, and it
// lives in the object's separate script list rather than its component map) - a script wanting to know its
// own object's scripts already has that through ScriptBase.
//
// addComponent/removeComponent apply to the object immediately, not buffered past the current tick:
// Object::addComponent/removeComponent for a non-Script type only ever touch m_components (the object's
// own ComponentType-keyed map), never the m_scripts vector or the ObjectManager's own object list that
// ScriptSystem's fixedUpdate/variableUpdate range over - so mutating one object's non-script components
// mid-pass cannot invalidate either loop, even when the object being mutated is the one currently running.
// hasComponent called later in the same tick already sees the change. Replication is still deferred: the
// change is flagged on BindingContext (recordStructuralComponentChange) and the app re-broadcasts a full
// snapshot after the tick - the same structural path the editor's own sceneEdit addComponent/
// removeComponent ops take, reused here rather than inventing a script-specific wire message.
struct ComponentOpsBindings
{
  bool(*hasComponent)(const char* uuid, const char* componentType);
  bool(*addComponent)(const char* uuid, const char* componentType);
  bool(*removeComponent)(const char* uuid, const char* componentType);
  const char*(*getComponentTypes)(const char* uuid);
};

class ComponentOpsBindingsProvider {
public:
  [[nodiscard]] static ComponentOpsBindings getBindings();

private:
  static bool bindHasComponent(const char* uuid, const char* componentType);
  static bool bindAddComponent(const char* uuid, const char* componentType);
  static bool bindRemoveComponent(const char* uuid, const char* componentType);

  // Comma-delimited list of the registry-key names of every component the object owns (Transform
  // included, Script excluded) - the same delimiting convention as WorldBindings::getAllObjectUuids.
  static const char* bindGetComponentTypes(const char* uuid);
};



#endif //COMPONENTOPSBINDINGS_H
