#ifndef TERRAINRESTINGFIXTURES_H
#define TERRAINRESTINGFIXTURES_H

#include "CollisionSystem.h"
#include "ConvexMeshFixtures.h"
#include "PhysicsSystem.h"
#include "PhysicsTestHelpers.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/CapsuleCollider.h"
#include "objects/components/collisions/ConvexHull.h"
#include "objects/components/collisions/ConvexMeshCollider.h"
#include "objects/components/collisions/SphereCollider.h"
#include "objects/components/collisions/Terrain.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <gtest/gtest.h>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Shared by the terrain quality gate (TerrainRestingTest.cpp and TerrainGateTest.cpp): scenes that must hold in the
// headless suite with the solver and the generator as they are. Every scene runs at the server's tick (50 Hz), on
// terrain with the engine's default voxel size (2.5 units, half a meter at 5 units per meter), and prints what it
// measured.
namespace terrainResting {
  using Field = std::function<float(glm::vec3)>;

  constexpr float tick = 1.0f / 50.0f;
  constexpr int ticksPerSecond = 50;
  constexpr float voxel = 2.5f;
  // One millimeter, at 5 world units per meter.
  constexpr float millimeter = 0.005f;
  constexpr float penetrationSlop = 0.0005f;
  constexpr float pi = 3.14159265358979f;
  constexpr float center = 120.0f;
  constexpr int settleSeconds = 3;
  constexpr int observeSeconds = 60;

  // Samples cover x and z in [0, 96) and y in [-32, 64), which is 240 units across at the default voxel size.
  inline void fillField(terrain::TerrainVolume& volume, const Field& signedDistanceUnits)
  {
    const float voxelSize = volume.voxelSize();

    for (const terrain::ChunkCoord coord : terrain::chunksTouched({ { 0, -32, 0 }, { 96, 64, 96 } }))
    {
      std::vector<int16_t> distances(terrain::samplesPerChunk);
      std::vector<uint8_t> materials(terrain::samplesPerChunk, 1);
      const glm::ivec3 origin = terrain::chunkOrigin(coord);

      for (int z = 0; z < terrain::chunkSize; ++z)
      {
        for (int y = 0; y < terrain::chunkSize; ++y)
        {
          for (int x = 0; x < terrain::chunkSize; ++x)
          {
            const glm::vec3 position = voxelSize * glm::vec3(origin + glm::ivec3(x, y, z));
            distances[static_cast<size_t>(terrain::sampleIndex(x, y, z))] =
              terrain::encodeDistance(signedDistanceUnits(position) / voxelSize);
          }
        }
      }

      volume.setChunk(coord, terrain::TerrainChunk::dense(std::move(distances), std::move(materials)));
    }
  }

  inline Field flatGround()
  {
    return [](const glm::vec3 p) { return p.y; };
  }

  inline glm::vec3 slopeNormal(const float degrees)
  {
    const float angle = glm::radians(degrees);

    return { -std::sin(angle), std::cos(angle), 0.0f };
  }

  // Solid below the plane through the origin that rises along +x.
  inline Field slopeGround(const float degrees)
  {
    const glm::vec3 normal = slopeNormal(degrees);

    return [normal](const glm::vec3 p) { return glm::dot(p, normal); };
  }

  // Solid below y = |x - center|: a valley whose two walls are 45 degrees.
  inline Field valleyGround()
  {
    return [](const glm::vec3 p) { return (p.y - std::abs(p.x - center)) / std::sqrt(2.0f); };
  }

  // The exact distance to the solid wedge below a roof whose two faces fall away from x = center at the given angle.
  inline Field roofGround(const float degrees)
  {
    const float angle = glm::radians(degrees);
    const glm::vec2 face(std::cos(angle), -std::sin(angle));

    return [face](const glm::vec3 p)
    {
      const glm::vec2 offset(std::abs(p.x - center), p.y);
      const float along = std::max(0.0f, glm::dot(offset, face));
      const float distance = glm::length(offset - along * face);

      return offset.y < offset.x * face.y / face.x ? -distance : distance;
    };
  }

  // Solid below a bowl-shaped dimple of the given radius whose rim is at y = 0.
  inline Field dimpleGround(const float radius)
  {
    return [radius](const glm::vec3 p)
    {
      const float toBowl = glm::distance(p, glm::vec3(center, 0.0f, center));

      return std::max(p.y, radius - toBowl);
    };
  }

  struct Rolling {
    float amplitude;
    float wavelength;

    [[nodiscard]] float height(const float x, const float z) const
    {
      const float wave = 2.0f * pi / wavelength;

      return amplitude * std::sin(wave * x) * std::sin(wave * z);
    }

    // The first order distance estimate the terrain generators use: the vertical gap over sqrt(1 + |grad h|^2).
    [[nodiscard]] Field field() const
    {
      const Rolling self = *this;

      return [self](const glm::vec3 p)
      {
        const float wave = 2.0f * pi / self.wavelength;
        const float dx = self.amplitude * wave * std::cos(wave * p.x) * std::sin(wave * p.z);
        const float dz = self.amplitude * wave * std::sin(wave * p.x) * std::cos(wave * p.z);

        return (p.y - self.height(p.x, p.z)) / std::sqrt(1.0f + dx * dx + dz * dz);
      };
    }
  };

  // The same ground moved sideways, which puts its features at another place against the voxel lattice.
  inline Field shifted(const Field& field, const float by)
  {
    return [field, by](const glm::vec3 p) { return field({ p.x - by, p.y, p.z - by }); };
  }

  // A shift of 0.37 voxel puts a feature off the lattice; 0.5 voxel puts it in the middle of a cell.
  constexpr float offLattice = 0.37f * voxel;
  constexpr float midCell = 0.5f * voxel;

  inline Rolling rollingTerrain()
  {
    return { 1.5f, 80.0f };
  }

  enum class Shape { box, sphere, capsule, hull };

  struct Body {
    Shape shape = Shape::box;
    std::shared_ptr<Object> object;
    std::shared_ptr<RigidBody> body;
    std::shared_ptr<ConvexMeshCollider> hull;
    glm::vec3 halfExtents{ 1.0f };

    static constexpr float sphereRadius = 1.0f;
    static constexpr float capsuleRadius = 0.5f;
    static constexpr float capsuleCoreHalf = 0.5f;

    [[nodiscard]] glm::vec3 position() const
    {
      return fixtures::positionOf(object);
    }

    [[nodiscard]] glm::quat orientation() const
    {
      return fixtures::transformOf(object)->getOrientation();
    }

    // The distance every query point must stay above the ground.
    [[nodiscard]] float radius() const
    {
      switch (shape)
      {
        case Shape::sphere:
          return sphereRadius;
        case Shape::capsule:
          return capsuleRadius;
        default:
          return 0.0f;
      }
    }

    // The points that give away a turn: a box's corners, a capsule's ends, a hull's vertices, and a mark on a sphere.
    [[nodiscard]] std::vector<glm::vec3> extremes() const
    {
      const glm::quat turn = orientation();
      std::vector<glm::vec3> result;
      if (shape == Shape::sphere)
      {
        result = { position(), position() + turn * glm::vec3(sphereRadius, 0, 0) };
      }
      else if (shape == Shape::box)
      {
        for (const float sx : { -1.0f, 1.0f })
        {
          for (const float sy : { -1.0f, 1.0f })
          {
            for (const float sz : { -1.0f, 1.0f })
            {
              result.push_back(position() + turn * (halfExtents * glm::vec3(sx, sy, sz)));
            }
          }
        }
      }
      else
      {
        result = points();
      }

      return result;
    }

    // Each of the body's extreme points in world space, and for a box a fine grid over its bottom face too: over a
    // ridge it is the face's interior that touches, not its corners.
    [[nodiscard]] std::vector<glm::vec3> points() const
    {
      const glm::quat turn = orientation();
      const glm::vec3 at = position();
      std::vector<glm::vec3> result;

      switch (shape)
      {
        case Shape::sphere:
          result.push_back(at);
          break;
        case Shape::capsule:
          result.push_back(at + turn * glm::vec3(0, capsuleCoreHalf, 0));
          result.push_back(at - turn * glm::vec3(0, capsuleCoreHalf, 0));
          break;
        case Shape::hull:
          result = hull->getWorldVertices();
          break;
        case Shape::box:
        {
          for (const float sx : { -1.0f, 1.0f })
          {
            for (const float sy : { -1.0f, 1.0f })
            {
              for (const float sz : { -1.0f, 1.0f })
              {
                result.push_back(at + turn * (halfExtents * glm::vec3(sx, sy, sz)));
              }
            }
          }

          constexpr float step = 1.25f;
          const int columns = std::max(1, static_cast<int>(std::ceil(2.0f * halfExtents.x / step)));
          const int rows = std::max(1, static_cast<int>(std::ceil(2.0f * halfExtents.z / step)));
          for (int i = 0; i <= columns; ++i)
          {
            for (int j = 0; j <= rows; ++j)
            {
              const float x = -halfExtents.x + 2.0f * halfExtents.x * static_cast<float>(i) / static_cast<float>(columns);
              const float z = -halfExtents.z + 2.0f * halfExtents.z * static_cast<float>(j) / static_cast<float>(rows);
              result.push_back(at + turn * glm::vec3(x, -halfExtents.y, z));
            }
          }
          break;
        }
      }

      return result;
    }
  };

  struct World {
    fixtures::Scene scene = fixtures::makeScene();
    std::shared_ptr<terrain::TerrainVolume> volume = std::make_shared<terrain::TerrainVolume>(voxel);
    std::shared_ptr<Object> object;
    std::shared_ptr<Terrain> terrain = std::make_shared<Terrain>();
    Field ground;
    CollisionSystem collisions;

    explicit World(const Field& field, const glm::vec3& position = glm::vec3(0))
      : object(fixtures::addObject(scene, "Terrain", position)), ground(field)
    {
      fillField(*volume, field);
      object->addComponent(terrain);
      terrain->setVolume(volume);
    }

    // A world whose ground is a static box ramp with the same surface as slopeGround, for comparison.
    static World boxRamp(const float degrees)
    {
      World world(slopeGround(degrees));
      world.terrain->setCollisionEnabled(false);
      const auto ramp = fixtures::addObject(world.scene, "Ramp", -2.0f * slopeNormal(degrees) + glm::vec3(0, 0, center), { 400, 2, 100 });
      fixtures::transformOf(ramp)->setRotation({ 0, 0, degrees });
      fixtures::addBoxCollider(ramp);

      return world;
    }

    [[nodiscard]] glm::vec3 localOf(const glm::vec3& world) const
    {
      return world - fixtures::positionOf(object);
    }

    [[nodiscard]] glm::vec3 worldOf(const glm::vec3& local) const
    {
      return local + fixtures::positionOf(object);
    }

    void tickOnce()
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, tick);
      collisions.fixedUpdate(*scene.objectManager, tick);
    }

    void run(const int ticks)
    {
      for (int i = 0; i < ticks; ++i)
      {
        tickOnce();
      }
    }

    // How far the body's deepest point is inside the ground, in the given distance field: negative while above it.
    [[nodiscard]] float depth(const Body& body, const Field& distance) const
    {
      float deepest = -std::numeric_limits<float>::max();
      for (const glm::vec3& point : body.points())
      {
        deepest = std::max(deepest, body.radius() - distance(localOf(point)));
      }

      return deepest;
    }

    [[nodiscard]] float depth(const Body& body) const
    {
      return depth(body, ground);
    }
  };

  // A dynamic body with its lowest point at rest height above its origin. The hull is the tetrahedron on its base unless
  // another is given.
  inline Body addBody(World& world, const Shape shape, const glm::vec3& position, const glm::vec3& rotation,
               const float friction, const glm::vec3& scale = glm::vec3(1.0f), const ConvexHull& hull = {})
  {
    Body result;
    result.shape = shape;
    result.halfExtents = scale;
    result.object = fixtures::addObject(world.scene, "Body", position, scale);
    fixtures::transformOf(result.object)->setRotation(rotation);

    switch (shape)
    {
      case Shape::box:
        fixtures::addBoxCollider(result.object);
        break;
      case Shape::sphere:
        fixtures::addSphereCollider(result.object, Body::sphereRadius);
        break;
      case Shape::capsule:
        fixtures::addCapsuleCollider(result.object, Body::capsuleRadius, 2.0f);
        break;
      case Shape::hull:
        result.hull = std::make_shared<ConvexMeshCollider>();
        EXPECT_TRUE(result.hull->setHull(hull.vertices.empty() ? convexFixtures::tetrahedron() : hull));
        result.object->addComponent(result.hull);
        break;
    }

    result.body = physicsFixtures::addBody(result.object, true);
    result.body->setMass(1.0f);
    result.body->setFriction(friction);

    return result;
  }

  // What a run of the scene shows once the body has had its three seconds.
  struct Rest {
    float settleTime = 0.0f;       // seconds until the body stays within a millimeter of where it ends
    float drift = 0.0f;            // the furthest it strays from its place at three seconds, over the next sixty
    float deepest = 0.0f;          // the deepest point's depth over the same sixty seconds
    float deepestEver = 0.0f;      // the same from the first tick
    float finalSpeed = 0.0f;
    float height = 0.0f;           // the body's final world height
  };

  // The depth is that of grounded when given, which is for a body whose own is not the one touching the ground.
  inline Rest measureRest(World& world, const Body& body, const Field& distance, const float seconds = observeSeconds,
                   const Body* grounded = nullptr)
  {
    const int total = (settleSeconds + static_cast<int>(seconds)) * ticksPerSecond;
    using Pose = std::vector<glm::vec3>;
    std::vector<Pose> trail;
    trail.reserve(static_cast<size_t>(total));
    Rest rest;
    rest.deepest = -std::numeric_limits<float>::max();
    rest.deepestEver = rest.deepest;

    for (int i = 0; i < total; ++i)
    {
      world.tickOnce();
      trail.push_back(body.extremes());
      const float depth = world.depth(grounded ? *grounded : body, distance);
      rest.deepestEver = std::max(rest.deepestEver, depth);
      if (i >= settleSeconds * ticksPerSecond)
      {
        rest.deepest = std::max(rest.deepest, depth);
      }
    }

    const auto far = [](const Pose& a, const Pose& b)
    {
      float largest = 0.0f;
      for (size_t k = 0; k < a.size(); ++k)
      {
        largest = std::max(largest, glm::distance(a[k], b[k]));
      }

      return largest;
    };

    const auto& reference = trail[static_cast<size_t>(settleSeconds * ticksPerSecond - 1)];
    for (size_t i = static_cast<size_t>(settleSeconds * ticksPerSecond); i < trail.size(); ++i)
    {
      rest.drift = std::max(rest.drift, far(trail[i], reference));
    }

    size_t last = 0;
    for (size_t i = 0; i < trail.size(); ++i)
    {
      if (far(trail[i], trail.back()) > millimeter)
      {
        last = i + 1;
      }
    }
    rest.settleTime = static_cast<float>(last) * tick;
    rest.finalSpeed = glm::length(body.body->getVelocity()) / tick;
    rest.height = body.position().y;

    return rest;
  }

  inline Rest measureRest(World& world, const Body& body, const float seconds = observeSeconds, const Body* grounded = nullptr)
  {
    return measureRest(world, body, world.ground, seconds, grounded);
  }

  inline void report(const std::string& scene, const Rest& rest)
  {
    std::cout << "[gate] " << scene << ": settle " << rest.settleTime << " s, drift " << rest.drift / millimeter
              << " mm, depth " << rest.deepest / penetrationSlop << " slop (ever " << rest.deepestEver / penetrationSlop
              << "), speed " << rest.finalSpeed << "\n";
    ::testing::Test::RecordProperty(scene + " settleSeconds", std::to_string(rest.settleTime));
  }
}

#endif
