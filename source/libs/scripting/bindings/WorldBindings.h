#ifndef WORLDBINDINGS_H
#define WORLDBINDINGS_H

#include <cstdint>

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
  const char*(*spawnObject)(const char* name, float x, float y, float z);
  void(*destroyObject)(const char* uuid);
  const char*(*raycast)(float ox, float oy, float oz, float dx, float dy, float dz,
                        float maxDistance, uint32_t layerMask, const char* ignoreUuid);
  const char*(*overlapSphere)(float cx, float cy, float cz, float radius, uint32_t layerMask,
                              const char* ignoreUuid);
  const char*(*spawnPrefab)(const char* prefabUuid, float x, float y, float z);
};

class WorldBindingsProvider {
public:
  [[nodiscard]] static WorldBindings getBindings();

private:
  static const char* bindFindObjectByName(const char* name);
  static const char* bindGetObjectName(const char* uuid);
  static bool bindObjectExists(const char* uuid);
  static const char* bindGetAllObjectUuids();

  // Spawn a minimal object (a Transform at the given position); returns its new uuid. destroyObject marks
  // the object for deletion through the existing path.
  static const char* bindSpawnObject(const char* name, float x, float y, float z);
  static void bindDestroyObject(const char* uuid);

  // Spawn a whole prefab (its subtree, with fresh uuids) at the given position; returns the new root's
  // uuid, or "" when the prefab uuid isn't a registered prefab or its body is malformed. The body is
  // resolved through the AssetRegistry injected into BindingContext.
  static const char* bindSpawnPrefab(const char* prefabUuid, float x, float y, float z);

  // Ray/overlap queries delegate to the sim implementation the app injected into BindingContext. Both
  // return a comma-delimited string the managed side parses: raycast → "uuid,dist,px,py,pz,nx,ny,nz" (or
  // "" on a miss); overlapSphere → a comma-separated uuid list (or "").
  static const char* bindRaycast(float ox, float oy, float oz, float dx, float dy, float dz,
                                 float maxDistance, uint32_t layerMask, const char* ignoreUuid);
  static const char* bindOverlapSphere(float cx, float cy, float cz, float radius, uint32_t layerMask,
                                       const char* ignoreUuid);
};



#endif //WORLDBINDINGS_H
