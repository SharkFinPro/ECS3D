#ifndef RIGIDBODYBINDINGS_H
#define RIGIDBODYBINDINGS_H

struct RigidBodyBindings
{
  void(*applyForce)(const char* uuid, float x, float y, float z, float px, float py, float pz);
  void(*setVelocity)(const char* uuid, float x, float y, float z);
  bool(*isFalling)(const char* uuid);
  bool(*has)(const char* uuid);
  // New fields go at the END to keep the layout matched with the C# RigidBodyBindings struct.
  void(*setAngularVelocity)(const char* uuid, float x, float y, float z);
  void(*getVelocity)(const char* uuid, float* x, float* y, float* z);
  void(*getAngularVelocity)(const char* uuid, float* x, float* y, float* z);
  float(*getMass)(const char* uuid);
  void(*setMass)(const char* uuid, float mass);
  float(*getFriction)(const char* uuid);
  void(*setFriction)(const char* uuid, float friction);
  float(*getGravity)(const char* uuid);
  void(*setGravity)(const char* uuid, float gravity);
  bool(*getDoGravity)(const char* uuid);
  void(*setDoGravity)(const char* uuid, bool doGravity);
};

class RigidBodyBindingsProvider {
public:
  [[nodiscard]] static RigidBodyBindings getBindings();

private:
  // find() resolves uuid -> RigidBody via BindingContext. applyForce queues a force on the RigidBody
  // data (PhysicsSystem drains it each tick) so scripting stays independent of ECS3DSim.
  static void bindApplyForce(const char* uuid, float x, float y, float z, float px, float py, float pz);
  static void bindSetVelocity(const char* uuid, float x, float y, float z);

  // Overwrite the angular velocity (degrees per second). A player script zeroes it to keep mouse-look authoritative
  // (physics integrates rotation from angular velocity, which a collision-induced spin would otherwise fight).
  static void bindSetAngularVelocity(const char* uuid, float x, float y, float z);

  static bool bindIsFalling(const char* uuid);

  // Whether the object identified by uuid currently has a RigidBody (backs World.tryGetRigidBody).
  static bool bindHas(const char* uuid);

  static void bindGetVelocity(const char* uuid, float* x, float* y, float* z);
  static void bindGetAngularVelocity(const char* uuid, float* x, float* y, float* z);

  static float bindGetMass(const char* uuid);
  // Not covered by the per-tick state delta (Transform only), so mutating setters record a component
  // edit for BindingContext/ServerApp to replicate, the same as ModelRendererBindings.
  static void bindSetMass(const char* uuid, float mass);

  static float bindGetFriction(const char* uuid);
  static void bindSetFriction(const char* uuid, float friction);

  static float bindGetGravity(const char* uuid);
  static void bindSetGravity(const char* uuid, float gravity);

  static bool bindGetDoGravity(const char* uuid);
  static void bindSetDoGravity(const char* uuid, bool doGravity);
};



#endif //RIGIDBODYBINDINGS_H
