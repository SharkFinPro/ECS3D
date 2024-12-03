#ifndef COLLIDER_H
#define COLLIDER_H

#include "../Component.h"
#include <memory>
#include <glm/vec3.hpp>

#include "Simplex.h"

class Transform;

class Collider : public Component {
public:
  Collider();

  bool collidesWith(const std::shared_ptr<Object>& other, glm::vec3* mtv);

private:
  glm::vec3 getSupport(const std::shared_ptr<Collider>& other, const glm::vec3& direction);
  virtual glm::vec3 findFurthestPoint(const glm::vec3& direction) = 0;

  static bool expandSimplex(Simplex& simplex, glm::vec3& direction);
  static bool lineCase(const Simplex& simplex, glm::vec3& direction);
  static bool triangleCase(Simplex& simplex, glm::vec3& direction);
  static bool tetrahedronCase(Simplex& simplex, glm::vec3& direction);

protected:
  std::weak_ptr<Transform> transform_ptr;
};



#endif //COLLIDER_H
