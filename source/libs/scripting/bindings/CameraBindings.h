#ifndef CAMERABINDINGS_H
#define CAMERABINDINGS_H

// New fields go at the END to keep the layout matched with the C# CameraBindings struct.
struct CameraBindings
{
  void(*getDirection)(const char* uuid, float* x, float* y, float* z);
  bool(*has)(const char* uuid);
  void(*setDirection)(const char* uuid, float x, float y, float z);
  float(*getFov)(const char* uuid);
  void(*setFov)(const char* uuid, float fov);
  float(*getNearPlane)(const char* uuid);
  void(*setNearPlane)(const char* uuid, float nearPlane);
  float(*getFarPlane)(const char* uuid);
  void(*setFarPlane)(const char* uuid, float farPlane);
  bool(*isActive)(const char* uuid);
  void(*setActive)(const char* uuid, bool active);
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

  // Not covered by the per-tick state delta (Transform only), so mutating setters record a component
  // edit for BindingContext/ServerApp to replicate, the same as ModelRendererBindings.
  static void bindSetDirection(const char* uuid, float x, float y, float z);

  static float bindGetFov(const char* uuid);
  static void bindSetFov(const char* uuid, float fov);

  static float bindGetNearPlane(const char* uuid);
  static void bindSetNearPlane(const char* uuid, float nearPlane);

  static float bindGetFarPlane(const char* uuid);
  static void bindSetFarPlane(const char* uuid, float farPlane);

  static bool bindIsActive(const char* uuid);
  static void bindSetActive(const char* uuid, bool active);
};



#endif //CAMERABINDINGS_H
