#ifndef COLLISIONSYSTEM_H
#define COLLISIONSYSTEM_H

#include <memory>

class SceneAsset;

class CollisionSystem {
public:
  void fixedUpdate(const std::shared_ptr<SceneAsset>& scene, float dt);

private:
  // TODO: migrate CollisionManager sweep-and-prune (LeftEdge/collisionEdges, checkCollisions,
  // TODO:   handleCollisions) and the GJK/EPA narrow phase from Collider.cpp (collidesWith,
  // TODO:   getSupport, expandSimplex, line/triangle/tetrahedron cases) + Polytope + Simplex.
};



#endif //COLLISIONSYSTEM_H
