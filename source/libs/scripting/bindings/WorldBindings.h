#ifndef WORLDBINDINGS_H
#define WORLDBINDINGS_H

// Scene/world queries exposed to scripts. Mirrors the TransformBindings pattern: a C-ABI struct of
// function pointers registered with the managed side through Bridge, resolved against the server's
// current ObjectManager via BindingContext (set by ScriptSystem each tick).
//
// String returns point into a thread-local buffer owned by the native side; the managed caller marshals
// them out immediately (see World.cs) and must not free them. String arguments are owned by the caller.
struct WorldBindings
{
  const char*(*findObjectByName)(const char* name);
  const char*(*getObjectName)(const char* uuid);
  bool(*objectExists)(const char* uuid);
  const char*(*getAllObjectUuids)();
};

class WorldBindingsProvider {
public:
  [[nodiscard]] static WorldBindings getBindings();

private:
  static const char* bindFindObjectByName(const char* name);
  static const char* bindGetObjectName(const char* uuid);
  static bool bindObjectExists(const char* uuid);
  static const char* bindGetAllObjectUuids();
};



#endif //WORLDBINDINGS_H
