#ifndef COMPONENTOPSBINDINGS_H
#define COMPONENTOPSBINDINGS_H

// Generic add/remove/query component operations exposed to scripts, keyed by the ComponentRegistry's
// type-name strings (the same names Object::loadFromJSON/ComponentRegistry::create use - "RigidBody",
// "Box", "Sphere", ... - see componentTypeToRegistryKey in Component.h) rather than a native enum:
// ComponentType's packed value is the wire discriminator, assigned by enumerator order, so a name the
// managed side derived independently could drift from what the native side actually packs.
//
// Transform is refused by addComponent/removeComponent (it is structural - every other system assumes an
// object has one) but still shows up in hasComponent/getComponentTypes, since querying it is harmless and
// it is a real component the object owns. Script is excluded everywhere (adding one needs a class name
// this API does not take, a script adds another script through a separate, not-yet-built call, and it
// lives in the object's separate script list rather than its component map) - a script wanting to know its
// own object's scripts already has that through ScriptBase. An object ObjectManager::isMarkedForDeletion
// already reports as queued for removal this tick refuses both too: a change made to it now would never
// reach a client, since deleteObjectsMarkedForDeletion runs after this tick's structural broadcast.
//
// addComponent/removeComponent apply to the object immediately, not buffered past the current tick:
// Object::addComponent/removeComponent for a non-Script type only ever touch m_components (the object's
// own ComponentType-keyed map), a map ScriptSystem's fixedUpdate/variableUpdate loops (which range over
// the ObjectManager's object list and each object's m_scripts vector) do not read - so mutating one
// object's non-script components mid-pass leaves both loops' iterators alone, even when the object being
// mutated is the one currently running. hasComponent called later in the same tick already sees the
// change. Replication is still deferred and batched: the changed object's uuid is recorded on
// BindingContext (recordStructuralComponentChange, deduped per tick) and ServerApp::broadcastStructuralChanges
// sends that object's current packed state as an objectComponentsChanged message after the tick - a
// receiver finds the object by uuid and unpacks into it in place, rather than the whole project
// re-snapshotting the way the editor's own sceneEdit addComponent/removeComponent ops do.
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
