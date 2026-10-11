#ifndef PHYSICSTESTHELPERS_H
#define PHYSICSTESTHELPERS_H

#include "TestScene.h"
#include "objects/Object.h"
#include "objects/components/RigidBody.h"
#include "objects/components/collisions/Collider.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <array>
#include <memory>

// Shared setup for the PhysicsIntegration suites, which are split across the Physics*Test.cpp files.
namespace physicsFixtures {
  // The tick length the engine runs at. Every number below is derived from it rather than measured, so
  // a change to the timestep shows up as a failure that names the arithmetic rather than a mystery.
  inline constexpr float dt = 0.1f;

  // What integrate() adds to a body's velocity each tick under the default gravity. Note that integrate
  // moves by the velocity directly rather than by velocity * dt, so "velocity" is displacement per tick -
  // while the rotation it applies below it does use dt.
  inline constexpr float gravityPerTick = -9.81f * dt * 0.1f;

  // The numbers below are also derived from RigidBody's defaults, not just from dt. Pinned here so that
  // changing one produces a failure that names the default rather than a page of unexplained arithmetic.
  inline constexpr float defaultGravity = -9.81f;
  inline constexpr float defaultMass = 10.0f;

  // The collider on the body's own object: the one that makes the contact when the body is not compound.
  inline std::shared_ptr<Collider> ownColliderOf(const RigidBody& body)
  {
    return body.getOwner()->getComponent<Collider>(ComponentType::collider);
  }

  inline std::shared_ptr<RigidBody> addBody(const std::shared_ptr<Object>& object, const bool gravity)
  {
    auto body = fixtures::addRigidBody(object);
    body->setDoGravity(gravity);

    return body;
  }

  // A unit square underside a box centered at the origin rests on, as a box-on-box manifold reports it.
  inline constexpr std::array<glm::vec3, 4> flatUnderside{
    glm::vec3{ -0.5f, -0.5f, -0.5f }, glm::vec3{ 0.5f, -0.5f, -0.5f },
    glm::vec3{ 0.5f, -0.5f, 0.5f }, glm::vec3{ -0.5f, -0.5f, 0.5f }
  };

  // Looser than the fixture default: these values come out of an accumulation over several ticks, not
  // out of a single operation.
  inline void expectNear(const char* what, const glm::vec3& actual, const glm::vec3& expected)
  {
    fixtures::expectNear(what, actual, expected, 1e-4f);
  }

  // A body's spin in radians per tick, the units a push is in, rather than the degrees per second it is
  // stored in.
  inline glm::vec3 spinPerTickOf(const RigidBody& body)
  {
    return glm::radians(body.getAngularVelocity()) * dt;
  }

  // Where a body's local up axis points, composed the way BoxCollider places its vertices, so the tests
  // read a rotation the way collisions do rather than through the code under test.
  inline glm::vec3 localUpOf(const glm::vec3& rotationDegrees)
  {
    const auto orientation = glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.z), { 0, 0, 1 })
      * glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.y), { 0, 1, 0 })
      * glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.x), { 1, 0, 0 });

    return glm::vec3(orientation * glm::vec4(0, 1, 0, 0));
  }
}

#endif
