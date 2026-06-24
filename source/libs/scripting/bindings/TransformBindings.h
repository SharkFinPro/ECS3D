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
};

class TransformBindingsProvider {
public:
  [[nodiscard]] static TransformBindings getBindings();

private:
  // TODO: replace the old static ECS3D* s_ecs lookup with a scene handle from the SimContext, so
  // TODO:   find() can resolve a uuid against the server's authoritative ObjectManager.
  static void bindGetPosition(const char* uuid, float* x, float* y, float* z);
  static void bindGetScale(const char* uuid, float* x, float* y, float* z);
  static void bindGetRotation(const char* uuid, float* x, float* y, float* z);

  static void bindSetScale(const char* uuid, float x, float y, float z);
  static void bindSetRotation(const char* uuid, float x, float y, float z);

  static void bindMove(const char* uuid, float x, float y, float z);

  static void bindStart(const char* uuid);
  static void bindStop(const char* uuid);
};



#endif //TRANSFORMBINDINGS_H
