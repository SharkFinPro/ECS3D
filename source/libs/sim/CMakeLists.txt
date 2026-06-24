project(ECS3DSim)

add_library(${PROJECT_NAME}
  PhysicsSystem.cpp
  PhysicsSystem.h
  CollisionSystem.cpp
  CollisionSystem.h
)

target_include_directories(${PROJECT_NAME} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
  ECS3DData
)

# TODO: migrate the physics from RigidBody.cpp (fixedUpdate integration, applyForce,
# TODO:   handleCollision/respondToCollision, getInertiaTensor, limitMovement) into PhysicsSystem,
# TODO:   and CollisionManager (sweep-and-prune) + the GJK/EPA from Collider.cpp/Polytope/Simplex
# TODO:   into CollisionSystem.

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
  target_link_libraries(${PROJECT_NAME} PUBLIC OpenMP::OpenMP_CXX)
endif()
