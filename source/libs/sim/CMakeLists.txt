project(ECS3DSim)

add_library(${PROJECT_NAME}
  PhysicsSystem.cpp
  PhysicsSystem.h
  FixedTimestep.cpp
  FixedTimestep.h
  ComponentAccess.h
  InertiaTensor.cpp
  InertiaTensor.h
  CollisionSystem.cpp
  CollisionSystem.h
  ContactSolver.cpp
  ContactSolver.h
  collisions/NarrowPhase.cpp
  collisions/NarrowPhase.h
  collisions/Simplex.cpp
  collisions/Simplex.h
  collisions/Polytope.cpp
  collisions/Polytope.h
  collisions/ContactManifold.cpp
  collisions/ContactManifold.h
  collisions/FaceManifold.cpp
  collisions/FaceManifold.h
  collisions/ManifoldInternal.h
  collisions/PlanarHull.cpp
  collisions/PlanarHull.h
  collisions/Support.cpp
  collisions/Support.h
  collisions/SupportFaces.cpp
  collisions/SupportFaces.h
  queries/HullQueries.cpp
  queries/HullQueries.h
  queries/SceneQueries.cpp
  queries/SceneQueries.h
)

target_include_directories(${PROJECT_NAME} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
  ECS3DData
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  ECS3DProfile
)

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
  target_link_libraries(${PROJECT_NAME} PUBLIC OpenMP::OpenMP_CXX)
endif()
