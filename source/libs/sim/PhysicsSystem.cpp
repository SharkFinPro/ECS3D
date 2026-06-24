#include "PhysicsSystem.h"

void PhysicsSystem::fixedUpdate(const std::shared_ptr<SceneAsset>& scene, const float dt, SimContext& context)
{
  // TODO: walk every object in the scene with a RigidBody + Transform and call integrate() on it,
  // TODO:   resolving collisions reported by CollisionSystem via handleCollision(). This is the loop
  // TODO:   that used to run inside RigidBody::fixedUpdate through SceneManager::fixedUpdate.
  (void)scene;
  (void)dt;
  (void)context;
}

void PhysicsSystem::integrate(RigidBody& body, Transform& transform, const float dt)
{
  // TODO: port RigidBody::fixedUpdate: apply gravity (applyForce), limitMovement, move the transform
  // TODO:   by the velocity, advance rotation by angularVelocity * dt, damp angularVelocity, and roll
  // TODO:   the falling/nextFalling state (body.setFalling(body.getNextFalling()) etc.).
  (void)body;
  (void)transform;
  (void)dt;
}

void PhysicsSystem::applyForce(RigidBody& body, Transform& transform, const glm::vec3& force, const glm::vec3& position)
{
  // TODO: port RigidBody::applyForce: add force to velocity, update falling on upward reversal, then
  // TODO:   add the angular impulse cross(position - transform.getPosition(), force) * inverse inertia.
  (void)body;
  (void)transform;
  (void)force;
  (void)position;
}

void PhysicsSystem::handleCollision(RigidBody& body, const std::shared_ptr<Object>& other,
                                    const glm::vec3 minimumTranslationVector, const glm::vec3 collisionPoint)
{
  // TODO: port RigidBody::handleCollision: respondToCollision, then resolve the impulse against the
  // TODO:   other object's RigidBody (read via Object::getComponent once Object lives in ECS3DData).
  (void)body;
  (void)other;
  (void)minimumTranslationVector;
  (void)collisionPoint;
}

void PhysicsSystem::respondToCollision(RigidBody& body, Transform& transform, const glm::vec3 minimumTranslationVector)
{
  // TODO: port RigidBody::respondToCollision: clear falling on an upward MTV, then move the transform
  // TODO:   out of penetration by the MTV.
  (void)body;
  (void)transform;
  (void)minimumTranslationVector;
}

void PhysicsSystem::limitMovement(RigidBody& body, Transform& transform)
{
  // TODO: port RigidBody::limitMovement: apply horizontal friction (-horizontalVelocity * friction)
  // TODO:   as a force at the transform's position.
  (void)body;
  (void)transform;
}

glm::mat3x3 PhysicsSystem::getInertiaTensor(const RigidBody& body, const Transform& transform)
{
  // TODO: port RigidBody::getInertiaTensor: solid-box tensor from the transform scale and body mass.
  (void)body;
  (void)transform;

  return glm::mat3x3(1.0f);
}
