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
  // TODO: applyForce/setVelocity are physics operations that now live in ECS3DSim PhysicsSystem,
  // TODO:   not on RigidBody. The bindings need a handle to the PhysicsSystem (via SimContext) to
  // TODO:   forward these calls, plus a scene handle to resolve uuid -> RigidBody.
  static void bindApplyForce(const char* uuid, float x, float y, float z, float px, float py, float pz);
  static void bindSetVelocity(const char* uuid, float x, float y, float z);

  static bool bindIsFalling(const char* uuid);
};



#endif //RIGIDBODYBINDINGS_H
