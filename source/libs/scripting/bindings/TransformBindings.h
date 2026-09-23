#ifndef TRANSFORMBINDINGS_H
#define TRANSFORMBINDINGS_H

// New fields go at the END to keep the layout matched with the C# TransformBindings struct.
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
  // getPosition/getScale/getRotation are parent-combined (world); these read this object's own local
  // values, matching Transform::getLocalPosition/getLocalScale/getLocalRotation.
  void(*getLocalPosition)(const char* uuid, float* x, float* y, float* z);
  void(*getLocalScale)(const char* uuid, float* x, float* y, float* z);
  void(*getLocalRotation)(const char* uuid, float* x, float* y, float* z);
  // Overwrites the local position outright (setScale/setRotation's sibling); move() is additive.
  void(*setPosition)(const char* uuid, float x, float y, float z);
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

  static void bindGetLocalPosition(const char* uuid, float* x, float* y, float* z);
  static void bindGetLocalScale(const char* uuid, float* x, float* y, float* z);
  static void bindGetLocalRotation(const char* uuid, float* x, float* y, float* z);

  static void bindSetPosition(const char* uuid, float x, float y, float z);
};



#endif //TRANSFORMBINDINGS_H
