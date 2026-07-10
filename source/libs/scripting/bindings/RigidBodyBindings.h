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
};

class RigidBodyBindingsProvider {
public:
  [[nodiscard]] static RigidBodyBindings getBindings();

private:
  // find() resolves uuid -> RigidBody via BindingContext. applyForce queues a force on the RigidBody
  // data (PhysicsSystem drains it each tick) so scripting stays independent of ECS3DSim.
  static void bindApplyForce(const char* uuid, float x, float y, float z, float px, float py, float pz);
  static void bindSetVelocity(const char* uuid, float x, float y, float z);

  // Overwrite the angular velocity (rad/s). A player script zeroes it to keep mouse-look authoritative
  // (physics integrates rotation from angular velocity, which a collision-induced spin would otherwise fight).
  static void bindSetAngularVelocity(const char* uuid, float x, float y, float z);

  static bool bindIsFalling(const char* uuid);

  // Whether the object identified by uuid currently has a RigidBody (backs World.tryGetRigidBody).
  static bool bindHas(const char* uuid);
};



#endif //RIGIDBODYBINDINGS_H
