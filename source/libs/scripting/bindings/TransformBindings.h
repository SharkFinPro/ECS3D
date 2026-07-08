#ifndef TRANSFORMBINDINGS_H
#define TRANSFORMBINDINGS_H

struct TransformBindings
{
  void(*getPosition)(const char* uuid, float* x, float* y, float* z);
  void(*getScale)(const char* uuid, float* x, float* y, float* z);
  void(*getRotation)(const char* uuid, float* x, float* y, float* z);
  void(*setScale)(const char* uuid, float x, float y, float z);
  void(*setRotation)(const char* uuid, float x, float y, float z);
  void(*move)(const char* uuid, float x, float y, float z);
  void(*start)(const char* uuid);
  void(*stop)(const char* uuid);
  bool(*has)(const char* uuid);
};

class TransformBindingsProvider {
public:
  [[nodiscard]] static TransformBindings getBindings();

private:
  // find() resolves a uuid against the server's ObjectManager via BindingContext (set by ScriptSystem).
  static void bindGetPosition(const char* uuid, float* x, float* y, float* z);
  static void bindGetScale(const char* uuid, float* x, float* y, float* z);
  static void bindGetRotation(const char* uuid, float* x, float* y, float* z);

  static void bindSetScale(const char* uuid, float x, float y, float z);
  static void bindSetRotation(const char* uuid, float x, float y, float z);

  static void bindMove(const char* uuid, float x, float y, float z);

  static void bindStart(const char* uuid);
  static void bindStop(const char* uuid);

  // Whether the object identified by uuid currently has a Transform (backs World.tryGetTransform).
  static bool bindHas(const char* uuid);
};



#endif //TRANSFORMBINDINGS_H
