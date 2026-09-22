#ifndef COLLIDERBINDINGS_H
#define COLLIDERBINDINGS_H

#include <cstdint>

// Collider script bindings: a shape-agnostic query (getShape) plus per-shape accessors, so a script can
// branch on shape without knowing every concrete type up front, and a new shape (capsule, convex mesh)
// can append its own group of fn ptrs at the end without reshaping what's here. Mirrors the
// ModelRendererBindings pattern: a C-ABI struct of function pointers, resolved against the server's
// current ObjectManager via BindingContext.
//
// getShape returns a small integer shared with the managed ColliderShape enum: 0 = none (no collider, or
// an unresolved uuid), 1 = box, 2 = sphere. The common get* accessors (isTrigger/layer/mask) return a
// neutral default when the object has no collider - the same convention as
// ModelRendererBindings::getShouldRender.
//
// The box-only and sphere-only accessors fail safely when the collider is the other shape (or missing):
// a getter returns false and leaves its out params untouched, a setter returns false and changes nothing.
// Every successful setter calls BindingContext::recordComponentEdit so the edit replicates (Collider
// isn't covered by the per-tick state delta, like ModelRenderer).
struct ColliderBindings
{
  int(*getShape)(const char* uuid);

  bool(*getIsTrigger)(const char* uuid);
  bool(*setIsTrigger)(const char* uuid, bool isTrigger);

  uint32_t(*getLayer)(const char* uuid);
  bool(*setLayer)(const char* uuid, uint32_t layer);

  uint32_t(*getMask)(const char* uuid);
  bool(*setMask)(const char* uuid, uint32_t mask);

  bool(*getBoxOffset)(const char* uuid, float* x, float* y, float* z);
  bool(*setBoxOffset)(const char* uuid, float x, float y, float z);
  bool(*getBoxSize)(const char* uuid, float* x, float* y, float* z);
  bool(*setBoxSize)(const char* uuid, float x, float y, float z);

  bool(*getSphereOffset)(const char* uuid, float* x, float* y, float* z);
  bool(*setSphereOffset)(const char* uuid, float x, float y, float z);
  bool(*getSphereRadius)(const char* uuid, float* radius);
  bool(*setSphereRadius)(const char* uuid, float radius);

  bool(*has)(const char* uuid);
};

class ColliderBindingsProvider {
public:
  [[nodiscard]] static ColliderBindings getBindings();

private:
  // find() resolves a uuid against the server's ObjectManager via BindingContext (set by ScriptSystem).
  static int bindGetShape(const char* uuid);

  static bool bindGetIsTrigger(const char* uuid);
  static bool bindSetIsTrigger(const char* uuid, bool isTrigger);

  static uint32_t bindGetLayer(const char* uuid);
  static bool bindSetLayer(const char* uuid, uint32_t layer);

  static uint32_t bindGetMask(const char* uuid);
  static bool bindSetMask(const char* uuid, uint32_t mask);

  // Box-only: false (out params untouched / component unchanged) when the collider is missing or is a
  // different shape.
  static bool bindGetBoxOffset(const char* uuid, float* x, float* y, float* z);
  static bool bindSetBoxOffset(const char* uuid, float x, float y, float z);
  static bool bindGetBoxSize(const char* uuid, float* x, float* y, float* z);
  static bool bindSetBoxSize(const char* uuid, float x, float y, float z);

  // Sphere-only: same fail-safe rule as the box accessors above.
  static bool bindGetSphereOffset(const char* uuid, float* x, float* y, float* z);
  static bool bindSetSphereOffset(const char* uuid, float x, float y, float z);
  static bool bindGetSphereRadius(const char* uuid, float* radius);
  static bool bindSetSphereRadius(const char* uuid, float radius);

  // Whether the object identified by uuid currently has a Collider of any shape (backs World.tryGetCollider).
  static bool bindHas(const char* uuid);
};



#endif //COLLIDERBINDINGS_H
