#ifndef CAMERABINDINGS_H
#define CAMERABINDINGS_H

struct CameraBindings
{
  void(*getDirection)(const char* uuid, float* x, float* y, float* z);
  bool(*has)(const char* uuid);
};

class CameraBindingsProvider {
public:
  [[nodiscard]] static CameraBindings getBindings();

private:
  // find() resolves a uuid against the server's ObjectManager via BindingContext (set by ScriptSystem).

  // Reads the object's Camera direction (its local look vector). Writes the forward default (0,0,-1) when
  // the object has no Camera, so a script that moves relative to the camera degrades safely.
  static void bindGetDirection(const char* uuid, float* x, float* y, float* z);

  // Whether the object identified by uuid currently has a Camera.
  static bool bindHas(const char* uuid);
};



#endif //CAMERABINDINGS_H
