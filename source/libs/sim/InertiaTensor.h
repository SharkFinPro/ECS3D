#ifndef INERTIATENSOR_H
#define INERTIATENSOR_H

#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>
#include <cstdint>
#include <span>
#include <vector>

class Object;
class Collider;
class Transform;

// Mass distributions of the colliders a rigid body is made of, and the inertia tensor they give it. Tensors
// are per unit mass, as a change of velocity turns a body whatever its mass.
namespace inertia {
  // A solid shape of uniform density in world space: its volume, its centroid, and the second moment of its
  // mass about the centroid per unit mass (the integral of x x^T over the shape, divided by its volume).
  struct Distribution {
    float volume = 0.0f;
    glm::vec3 centroid{ 0.0f };
    glm::mat3 covariance{ 0.0f };
  };

  [[nodiscard]] Distribution sphere(const glm::vec3& center, float radius);

  // axes holds the box's own axes as columns; halfExtents are along them.
  [[nodiscard]] Distribution box(const glm::vec3& center, const glm::mat3& axes, const glm::vec3& halfExtents);

  // The core segment swept by a sphere, so the caps are hemispheres on its ends.
  [[nodiscard]] Distribution capsule(const glm::vec3& start, const glm::vec3& end, float radius);

  // A closed triangle mesh in either winding. Zero volume for an empty or flat one.
  [[nodiscard]] Distribution hull(std::span<const glm::vec3> vertices, std::span<const uint32_t> indices);

  // Zero volume for a shape it has no closed form for, or one with no volume.
  [[nodiscard]] Distribution of(Collider& collider);

  // The distributions of the colliders that move with the body owned by owner: its own and those of its
  // descendants that do not carry a rigid body of their own. Triggers have no physical presence.
  [[nodiscard]] std::vector<Distribution> bodyDistributions(const Object& owner);

  // The tensor of the shapes taken together as one unit of mass, weighted by volume, about point.
  [[nodiscard]] glm::mat3 tensorAbout(std::span<const Distribution> shapes, const glm::vec3& point);

  // The body's tensor in world axes about its owner's position. A body with no collider volume is a solid box
  // whose half-extents are the transform's scale.
  [[nodiscard]] glm::mat3 worldTensor(const Object* owner, const Transform& transform);
}

#endif //INERTIATENSOR_H
