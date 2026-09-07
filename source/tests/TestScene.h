#ifndef TESTSCENE_H
#define TESTSCENE_H

#include <glm/vec3.hpp>
#include <memory>
#include <string>

class BoxCollider;
class ComponentRegistry;
class Object;
class ObjectManager;
class RigidBody;
class SphereCollider;
class Transform;

// Headless scaffolding shared by the suites that need a populated ObjectManager. Everything here builds
// the scene the way deserialization does - through the registry factories - so a fixture and a loaded
// project produce the same objects.
namespace fixtures {
  // Whether the scene's registry knows the data components. A scene built with `none` can create no
  // component at all, which is what a build being sent a type it does not have looks like - the case
  // that used to be a null dereference rather than an exception.
  enum class Components {
    registered,
    none
  };

  // Constructing one registers the data components and opens an empty manager on top of them, which is
  // the setup every suite below was repeating. Derive from it to hang extra members off the same scene.
  struct Scene {
    std::shared_ptr<ComponentRegistry> componentRegistry;
    std::unique_ptr<ObjectManager> objectManager;

    explicit Scene(Components components = Components::registered);
    // Out of line, and with the moves spelled out alongside: ObjectManager is incomplete here, and a
    // user-declared destructor would otherwise suppress the moves that returning a Scene needs.
    ~Scene();
    Scene(Scene&& other) noexcept;
    Scene& operator=(Scene&& other) noexcept;
  };

  [[nodiscard]] Scene makeScene();

  std::shared_ptr<Object> addObject(const Scene& scene, const std::string& name);

  std::shared_ptr<Object> addObject(const Scene& scene, const std::string& name,
                                    const glm::vec3& position);

  std::shared_ptr<Object> addObject(const Scene& scene, const std::string& name,
                                    const glm::vec3& position, const glm::vec3& scale);

  // Parented before registration, the way the manager expects a subtree to arrive.
  std::shared_ptr<Object> addChildObject(const Scene& scene, const std::string& name,
                                         const std::shared_ptr<Object>& parent);

  std::shared_ptr<BoxCollider> addBoxCollider(const std::shared_ptr<Object>& object);

  std::shared_ptr<SphereCollider> addSphereCollider(const std::shared_ptr<Object>& object, float radius);

  // Whether an object has one of these decides whether the collision sweep looks at it at all: an edge
  // with no rigid body on either end is never tested.
  std::shared_ptr<RigidBody> addRigidBody(const std::shared_ptr<Object>& object);

  // Throws rather than returning null: every caller dereferences the result on the same line, so a
  // message beats a crash in the line after.
  [[nodiscard]] std::shared_ptr<Transform> transformOf(const std::shared_ptr<Object>& object);

  [[nodiscard]] glm::vec3 positionOf(const std::shared_ptr<Object>& object);

  // Component-wise with a tolerance, because anything through a matrix, a normalize or an accumulation
  // is not bit-exact. Both vectors go in the trace, not just the component that failed: a failure reads
  // much better as "expected (0.9, 1, 0.9), got (0.9, 0.9, 0.9)" than as one number out of context.
  void expectNear(const char* what, const glm::vec3& actual, const glm::vec3& expected,
                  float tolerance = 1e-5f);

  void expectNear(const glm::vec3& actual, const glm::vec3& expected, float tolerance = 1e-5f);
}

#endif //TESTSCENE_H
