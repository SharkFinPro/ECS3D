#ifndef RIGIDBODYBINDINGS_H
#define RIGIDBODYBINDINGS_H

struct RigidBodyBindings
{
  void(*applyForce)(const char* uuid, float x, float y, float z, float px, float py, float pz);
  void(*setVelocity)(const char* uuid, float x, float y, float z);
  bool(*isFalling)(const char* uuid);
};

class RigidBodyBindingsProvider {
public:
  [[nodiscard]] static RigidBodyBindings getBindings();

private:
  // find() resolves uuid -> RigidBody via BindingContext. applyForce queues a force on the RigidBody
  // data (PhysicsSystem drains it each tick) so scripting stays independent of ECS3DSim.
  static void bindApplyForce(const char* uuid, float x, float y, float z, float px, float py, float pz);
  static void bindSetVelocity(const char* uuid, float x, float y, float z);

  static bool bindIsFalling(const char* uuid);
};



#endif //RIGIDBODYBINDINGS_H
