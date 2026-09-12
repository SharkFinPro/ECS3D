#ifndef MODELRENDERERBINDINGS_H
#define MODELRENDERERBINDINGS_H

// ModelRenderer script bindings: read/change what an object looks like (its model, texture, and whether
// it renders at all). Mirrors the TransformBindings pattern: a C-ABI struct of function pointers,
// resolved against the server's current ObjectManager via BindingContext.
//
// getModelUUID/getTextureUUID return a pointer into a thread-local buffer owned by the native side (the
// same convention as WorldBindings); the managed caller marshals the string out immediately and must not
// free it.
//
// setModelUUID/setTextureUUID validate the given asset uuid against the AssetRegistry injected into
// BindingContext: an unknown uuid, or one whose record is the wrong AssetType, fails safely - the
// component is left unchanged and false is returned. A successful set records the edit on BindingContext
// so the app can replicate it (ModelRenderer isn't covered by the per-tick state delta, unlike Transform).
struct ModelRendererBindings
{
  const char*(*getModelUUID)(const char* uuid);
  const char*(*getTextureUUID)(const char* uuid);
  bool(*getShouldRender)(const char* uuid);
  bool(*setModelUUID)(const char* uuid, const char* modelUUID);
  bool(*setTextureUUID)(const char* uuid, const char* textureUUID);
  void(*setShouldRender)(const char* uuid, bool shouldRender);
  bool(*has)(const char* uuid);
};

class ModelRendererBindingsProvider {
public:
  [[nodiscard]] static ModelRendererBindings getBindings();

private:
  // find() resolves a uuid against the server's ObjectManager via BindingContext (set by ScriptSystem).
  static const char* bindGetModelUUID(const char* uuid);
  static const char* bindGetTextureUUID(const char* uuid);
  static bool bindGetShouldRender(const char* uuid);

  // Assign by asset uuid, validated against the AssetRegistry (wrong/unknown uuid -> false, no change).
  static bool bindSetModelUUID(const char* uuid, const char* modelUUID);
  static bool bindSetTextureUUID(const char* uuid, const char* textureUUID);

  static void bindSetShouldRender(const char* uuid, bool shouldRender);

  // Whether the object identified by uuid currently has a ModelRenderer (backs World.tryGetModelRenderer).
  static bool bindHas(const char* uuid);
};



#endif //MODELRENDERERBINDINGS_H
