#include "CollisionSystem.h"

void CollisionSystem::fixedUpdate(const std::shared_ptr<SceneAsset>& scene, const float dt)
{
  // TODO: broad phase (sweep-and-prune) over collider bounding boxes, then narrow phase (GJK/EPA)
  // TODO:   to produce the minimum translation vector + collision point handed to PhysicsSystem.
  (void)scene;
  (void)dt;
}
