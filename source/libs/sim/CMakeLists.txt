project(ECS3DSim)

add_library(${PROJECT_NAME}
  PhysicsSystem.cpp
  PhysicsSystem.h
  CollisionSystem.cpp
  CollisionSystem.h
  collisions/Simplex.cpp
  collisions/Simplex.h
  collisions/Polytope.cpp
  collisions/Polytope.h
  collisions/Support.cpp
  collisions/Support.h
)

target_include_directories(${PROJECT_NAME} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
  ECS3DData
)

# CollisionSystem holds the migrated sweep-and-prune (CollisionManager) + GJK/EPA (Collider.cpp,
# Simplex, Polytope) — transplanted nearly verbatim.
# TODO: port the physics MATH bodies in PhysicsSystem (integrate, applyForce, handleCollision/
# TODO:   respondToCollision, getInertiaTensor, limitMovement) from RigidBody.cpp, and wire
# TODO:   CollisionSystem's collision response into PhysicsSystem::handleCollision.

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
  target_link_libraries(${PROJECT_NAME} PUBLIC OpenMP::OpenMP_CXX)
endif()
