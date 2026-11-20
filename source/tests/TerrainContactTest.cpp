#include <gtest/gtest.h>

#include "ConvexMeshFixtures.h"
#include "TestScene.h"
#include "collisions/TerrainContact.h"
#include "collisions/TerrainContactInternal.h"
#include "objects/Object.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/CapsuleCollider.h"
#include "objects/components/collisions/ConvexMeshCollider.h"
#include "objects/components/collisions/SphereCollider.h"
#include "objects/components/collisions/Terrain.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainSampling.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace {
  using Field = std::function<float(glm::vec3)>;

  constexpr float tolerance = 2e-2f;
  constexpr float tightTolerance = 1e-3f;
  constexpr float centerX = 48.0f;
  constexpr float centerZ = 48.0f;

  // Samples cover x and z in [0, 96) and y in [-32, 64), so a chunk border sits at x = 32 and z = 32.
  void fillField(terrain::TerrainVolume& volume, const Field& signedDistanceUnits)
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

  Field flatGround()
  {
    return [](const glm::vec3 p) { return p.y; };
  }

  glm::vec3 slopeNormal()
  {
    const float angle = glm::radians(20.0f);

    return { -std::sin(angle), std::cos(angle), 0.0f };
  }

  Field slopedGround()
  {
    return [](const glm::vec3 p) { return glm::dot(p, slopeNormal()); };
  }

  // Solid below y = |x - apexX|: a valley whose two walls are 45 degrees.
  Field valleyGround(const float apexX = centerX)
  {
    return [apexX](const glm::vec3 p) { return (p.y - std::abs(p.x - apexX)) / std::sqrt(2.0f); };
  }

  // A solid ball whose top is at y = 0.
  Field domeGround(const glm::vec3& center, const float radius)
  {
    return [center, radius](const glm::vec3 p) { return glm::distance(p, center) - radius; };
  }

  // Solid for x > centerX: a vertical wall facing -x.
  Field wallGround()
  {
    return [](const glm::vec3 p) { return centerX - p.x; };
  }

  // The exact distance to the solid wedge below y = -|x - apexX|, whose two faces slope at 45 degrees.
  Field roofGround(const float apexX)
  {
    return [apexX](const glm::vec3 p)
    {
      const float root = std::sqrt(0.5f);
      const glm::vec2 offset(std::abs(p.x - apexX), p.y);
      const glm::vec2 face(root, -root);
      const float along = std::max(0.0f, glm::dot(offset, face));
      const float distance = glm::length(offset - along * face);

      return offset.y < -offset.x ? -distance : distance;
    };
  }

  struct World {
    fixtures::Scene scene = fixtures::makeScene();
    std::shared_ptr<terrain::TerrainVolume> volume;
    std::shared_ptr<Object> object;
    std::shared_ptr<Terrain> terrain;

    explicit World(const Field& field, const glm::vec3& position = glm::vec3(0),
                   const glm::vec3& rotationDegrees = glm::vec3(0))
      : volume(std::make_shared<terrain::TerrainVolume>(1.0f)),
        object(fixtures::addObject(scene, "Terrain", position)),
        terrain(std::make_shared<Terrain>())
    {
      fillField(*volume, field);
      fixtures::transformOf(object)->setRotation(rotationDegrees);
      object->addComponent(terrain);
      terrain->setVolume(volume);
    }

    // A terrain-local position in world space.
    [[nodiscard]] glm::vec3 worldOf(const glm::vec3& local) const
    {
      const auto transform = fixtures::transformOf(object);

      return transform->getPosition() + transform->getOrientation() * local;
    }

    std::shared_ptr<SphereCollider> sphereAt(const glm::vec3& local, const float radius)
    {
      return fixtures::addSphereCollider(fixtures::addObject(scene, "Sphere", worldOf(local)), radius);
    }

    std::shared_ptr<CapsuleCollider> capsuleAt(const glm::vec3& local, const float radius, const float height,
                                               const glm::vec3& rotation = glm::vec3(0))
    {
      const auto object = fixtures::addObject(scene, "Capsule", worldOf(local));
      fixtures::transformOf(object)->setRotation(rotation);

      return fixtures::addCapsuleCollider(object, radius, height);
    }

    std::shared_ptr<BoxCollider> boxAt(const glm::vec3& local, const glm::vec3& halfExtents,
                                       const glm::vec3& rotation = glm::vec3(0))
    {
      const auto object = fixtures::addObject(scene, "Box", worldOf(local), halfExtents);
      fixtures::transformOf(object)->setRotation(rotation);

      return fixtures::addBoxCollider(object);
    }

    std::shared_ptr<ConvexMeshCollider> hullAt(const glm::vec3& local, const ConvexHull& hull)
    {
      const auto object = fixtures::addObject(scene, "Hull", worldOf(local));
      const auto collider = std::make_shared<ConvexMeshCollider>();
      EXPECT_TRUE(collider->setHull(hull));
      object->addComponent(collider);

      return collider;
    }
  };

  ConvexHull octahedron()
  {
    const std::vector<glm::vec3> points{ { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 },
                                         { 0, 0, -1 } };

    return *buildConvexHull(points);
  }

  std::vector<collisions::Contact> contactsOf(World& world, Collider& collider,
                                              const glm::vec3& sweep = glm::vec3(0))
  {
    return collisions::findTerrainContacts(collider, *world.terrain, sweep);
  }

  glm::vec3 up()
  {
    return { 0, 1, 0 };
  }

  void expectNormal(const collisions::Contact& contact, const glm::vec3& expected)
  {
    const auto normal = contact.normal();

    EXPECT_NEAR(normal.x, expected.x, 1e-2f);
    EXPECT_NEAR(normal.y, expected.y, 1e-2f);
    EXPECT_NEAR(normal.z, expected.z, 1e-2f);
  }

  float degreesBetween(const glm::vec3& first, const glm::vec3& second)
  {
    return glm::degrees(std::acos(std::clamp(glm::dot(glm::normalize(first), glm::normalize(second)), -1.0f, 1.0f)));
  }
}

TEST(TerrainContact, ASphereSinkingIntoFlatGroundReportsOneContactAlongTheSurfaceNormal)
{
  World world(flatGround());
  const auto sphere = world.sphereAt({ centerX, 0.8f, centerZ }, 1.0f);

  const auto contacts = contactsOf(world, *sphere);

  ASSERT_EQ(contacts.size(), 1u);
  expectNormal(contacts[0], up());
  EXPECT_NEAR(contacts[0].overlap(), 0.2f, tightTolerance);
  EXPECT_NEAR(contacts[0].depth(), 0.2f, tightTolerance);
  EXPECT_FLOAT_EQ(contacts[0].advance, 0.0f);
  EXPECT_EQ(contacts[0].contactPoints().size(), 1u);
  EXPECT_NEAR(contacts[0].depthAt(0), 0.2f, tightTolerance);
  fixtures::expectNear("point", contacts[0].point, { centerX, 0.0f, centerZ }, tolerance);
}

TEST(TerrainContact, AHoveringSphereIsReportedOnlyWithinTheMarginOrWhenTheSweepClosesTheGap)
{
  World world(flatGround());

  const auto hovering = world.sphereAt({ centerX, 1.05f, centerZ }, 1.0f);
  const auto withinMargin = contactsOf(world, *hovering);
  ASSERT_EQ(withinMargin.size(), 1u);
  EXPECT_NEAR(withinMargin[0].overlap(), -0.05f, tightTolerance);
  EXPECT_GT(withinMargin[0].depth(), 0.0f);

  const auto high = world.sphereAt({ centerX, 1.3f, centerZ }, 1.0f);
  EXPECT_TRUE(contactsOf(world, *high).empty());

  const auto swept = contactsOf(world, *high, { 0, -0.5f, 0 });
  ASSERT_EQ(swept.size(), 1u);
  EXPECT_NEAR(swept[0].overlap(), -0.3f, tightTolerance);
  EXPECT_NEAR(swept[0].advance, 0.5f, tightTolerance);
  EXPECT_NEAR(swept[0].depth(), 0.2f, tightTolerance);
}

TEST(TerrainContact, ASphereWithNothingBelowItIsApart)
{
  World world(flatGround());

  EXPECT_EQ(contactsOf(world, *world.sphereAt({ centerX, 0.8f, centerZ }, 1.0f)).size(), 1u);

  EXPECT_TRUE(contactsOf(world, *world.sphereAt({ centerX, 20.0f, centerZ }, 1.0f)).empty());
  EXPECT_TRUE(contactsOf(world, *world.sphereAt({ 200.0f, 0.8f, centerZ }, 1.0f)).empty());
  EXPECT_TRUE(contactsOf(world, *world.sphereAt({ centerX, 45.0f, centerZ }, 1.0f)).empty());
  EXPECT_TRUE(contactsOf(world, *world.sphereAt({ centerX, 45.0f, centerZ }, 1.0f), { 0, -0.5f, 0 }).empty());
}

TEST(TerrainContact, ARotatedTerrainTurnsTheNormalAndKeepsTheDepth)
{
  World world(flatGround(), glm::vec3(0), { 0, 0, 90 });
  const auto sphere = world.sphereAt({ centerX, 0.8f, centerZ }, 1.0f);

  const auto contacts = contactsOf(world, *sphere);

  ASSERT_EQ(contacts.size(), 1u);
  expectNormal(contacts[0], { -1, 0, 0 });
  EXPECT_NEAR(contacts[0].overlap(), 0.2f, tolerance);
  fixtures::expectNear("point", contacts[0].point, world.worldOf({ centerX, 0.0f, centerZ }), tolerance);
}

TEST(TerrainContact, ATranslatedAndTippedTerrainPlacesTheContactInWorldSpace)
{
  World world(flatGround(), { 10, 20, 30 });
  const auto sphere = world.sphereAt({ centerX, 0.8f, centerZ }, 1.0f);

  const auto contacts = contactsOf(world, *sphere);

  ASSERT_EQ(contacts.size(), 1u);
  expectNormal(contacts[0], up());
  EXPECT_NEAR(contacts[0].overlap(), 0.2f, tolerance);
  fixtures::expectNear("point", contacts[0].point, { 10 + centerX, 20.0f, 30 + centerZ }, tolerance);

  World tipped(flatGround(), { 10, 20, 30 }, { 90, 0, 0 });
  const auto tippedContacts = contactsOf(tipped, *tipped.sphereAt({ centerX, 0.8f, centerZ }, 1.0f));

  ASSERT_EQ(tippedContacts.size(), 1u);
  expectNormal(tippedContacts[0], { 0, 0, 1 });
  EXPECT_NEAR(tippedContacts[0].overlap(), 0.2f, tolerance);
  fixtures::expectNear("point", tippedContacts[0].point, tipped.worldOf({ centerX, 0.0f, centerZ }), tolerance);
}

TEST(TerrainContact, ATerrainInNegativeWorldCoordinatesPlacesTheContactThere)
{
  World world(flatGround(), { -100, -20, -100 });
  const auto sphere = world.sphereAt({ centerX, 0.8f, centerZ }, 1.0f);

  const auto contacts = contactsOf(world, *sphere);

  ASSERT_EQ(contacts.size(), 1u);
  expectNormal(contacts[0], up());
  EXPECT_NEAR(contacts[0].overlap(), 0.2f, tightTolerance);
  fixtures::expectNear("point", contacts[0].point, { -100 + centerX, -20.0f, -100 + centerZ }, tolerance);

  const auto apart = world.sphereAt({ centerX, 1.3f, centerZ }, 1.0f);
  EXPECT_TRUE(contactsOf(world, *apart).empty());
}

TEST(TerrainContact, ASphereOnASlopeGetsTheSlopeNormal)
{
  World world(slopedGround());
  const glm::vec3 onSlope(centerX, centerX * std::tan(glm::radians(20.0f)), centerZ);
  const auto sphere = world.sphereAt(onSlope + slopeNormal() * 0.8f, 1.0f);

  const auto contacts = contactsOf(world, *sphere);

  ASSERT_EQ(contacts.size(), 1u);
  EXPECT_LT(degreesBetween(contacts[0].normal(), slopeNormal()), 1.0f);
  EXPECT_NEAR(contacts[0].overlap(), 0.2f, tightTolerance);
}

TEST(TerrainContact, ASphereInAValleyTouchesBothWallsWhereFlatGroundGivesOne)
{
  const float radius = 3.0f;
  const float height = (radius - 0.3f) * std::sqrt(2.0f);

  World valley(valleyGround());
  const auto inValley = valley.sphereAt({ centerX, height, centerZ }, radius);
  const auto contacts = contactsOf(valley, *inValley);

  ASSERT_EQ(contacts.size(), 2u);
  const float root = std::sqrt(0.5f);
  const bool leftFirst = contacts[0].normal().x < 0.0f;
  expectNormal(contacts[leftFirst ? 0 : 1], { -root, root, 0 });
  expectNormal(contacts[leftFirst ? 1 : 0], { root, root, 0 });
  EXPECT_NEAR(contacts[0].overlap(), 0.3f, tolerance);
  EXPECT_NEAR(contacts[1].overlap(), 0.3f, tolerance);

  World flat(flatGround());
  const auto onFlat = contactsOf(flat, *flat.sphereAt({ centerX, radius - 0.3f, centerZ }, radius));

  ASSERT_EQ(onFlat.size(), 1u);
  expectNormal(onFlat[0], up());
  EXPECT_NEAR(onFlat[0].overlap(), 0.3f, tolerance);
}

TEST(TerrainContact, AValleyWhoseApexIsBetweenSamplesStillGivesExactlyTheTwoWalls)
{
  const float apexX = 48.37f;
  const float radius = 1.0f;
  const float offset = 0.09f;
  const float depthIntoNearWall = 0.28f;
  const float height = (radius - depthIntoNearWall) * std::sqrt(2.0f) + offset;

  World world(valleyGround(apexX));
  const auto sphere = world.sphereAt({ apexX + offset, height, centerZ }, radius);

  const auto contacts = contactsOf(world, *sphere);

  ASSERT_EQ(contacts.size(), 2u);
  const float root = std::sqrt(0.5f);
  const bool leftFirst = contacts[0].normal().x < 0.0f;
  const auto& left = contacts[leftFirst ? 0 : 1];
  const auto& right = contacts[leftFirst ? 1 : 0];
  expectNormal(left, { -root, root, 0 });
  expectNormal(right, { root, root, 0 });
  EXPECT_NEAR(left.overlap(), depthIntoNearWall, tolerance);
  EXPECT_NEAR(right.overlap(), depthIntoNearWall - std::sqrt(2.0f) * offset, tolerance);
}

TEST(TerrainContact, ASphereOnAConvexDomeGetsTheDomeDepthAndNormal)
{
  const float domeRadius = 6.0f;
  const glm::vec3 domeCenter(centerX + 0.3f, -domeRadius, centerZ + 0.2f);
  World world(domeGround(domeCenter, domeRadius));
  const glm::vec3 radial = glm::normalize(glm::vec3(0.4f, 1.0f, 0.2f));

  const auto resting = world.sphereAt(domeCenter + radial * (domeRadius + 1.0f - 0.1f), 1.0f);
  const auto contacts = contactsOf(world, *resting);

  ASSERT_EQ(contacts.size(), 1u);
  EXPECT_NEAR(contacts[0].overlap(), 0.1f, 0.05f);
  EXPECT_LT(degreesBetween(contacts[0].normal(), radial), 3.0f);

  const auto hovering = world.sphereAt(domeCenter + radial * (domeRadius + 1.0f + 0.3f), 1.0f);
  EXPECT_TRUE(contactsOf(world, *hovering).empty());
}

TEST(TerrainContact, ASphereTouchingASmallDomeIsNotReportedDeeperThanItTouches)
{
  const float domeRadius = 3.0f;
  const float radius = 2.0f;
  const glm::vec3 domeCenter(centerX + 0.3f, -domeRadius, centerZ + 0.2f);
  World world(domeGround(domeCenter, domeRadius));
  const glm::vec3 radial = glm::normalize(glm::vec3(-0.3f, 1.0f, 0.2f));

  const auto touching = world.sphereAt(domeCenter + radial * (domeRadius + radius), radius);
  const auto contacts = contactsOf(world, *touching);

  ASSERT_FALSE(contacts.empty());
  for (const auto& contact : contacts)
  {
    EXPECT_LT(contact.overlap(), 0.05f);
  }

  const auto sunk = world.sphereAt(domeCenter + radial * (domeRadius + radius - 0.1f), radius);
  const auto sunkContacts = contactsOf(world, *sunk);
  ASSERT_FALSE(sunkContacts.empty());
  EXPECT_NEAR(sunkContacts[0].overlap(), 0.1f, 0.05f);

  const auto hovering = world.sphereAt(domeCenter + radial * (domeRadius + radius + 0.3f), radius);
  EXPECT_TRUE(contactsOf(world, *hovering).empty());
}

TEST(TerrainContact, ASphereOnAFaceOfARoofRidgeGetsTheFaceNormalAndDepth)
{
  const float apexX = 48.37f;
  const float root = std::sqrt(0.5f);
  const glm::vec3 faceNormal(root, root, 0.0f);
  World world(roofGround(apexX));

  const auto onFace = [&world, apexX, root, faceNormal](const float alongFace, const float radius, const float depth)
  {
    const glm::vec3 surface(apexX + alongFace * root, -alongFace * root, centerZ + 0.2f);

    return world.sphereAt(surface + faceNormal * (radius - depth), radius);
  };

  const auto farFromApex = contactsOf(world, *onFace(4.0f, 1.0f, 0.1f));
  ASSERT_EQ(farFromApex.size(), 1u);
  EXPECT_NEAR(farFromApex[0].overlap(), 0.1f, 0.05f);
  EXPECT_LT(degreesBetween(farFromApex[0].normal(), faceNormal), 3.0f);

  EXPECT_TRUE(contactsOf(world, *onFace(4.0f, 1.0f, -0.3f)).empty());

  const auto nearApex = contactsOf(world, *onFace(1.5f, 2.0f, 0.0f));
  ASSERT_FALSE(nearApex.empty());
  for (const auto& contact : nearApex)
  {
    EXPECT_LT(contact.overlap(), 0.05f);
  }
  EXPECT_TRUE(contactsOf(world, *onFace(1.5f, 2.0f, -0.3f)).empty());
}

TEST(TerrainContact, ASphereOfTwoVoxelsRadiusOnFlatGroundIsFound)
{
  World world(flatGround());

  const auto contacts = contactsOf(world, *world.sphereAt({ centerX, 1.9f, centerZ }, 2.0f));

  ASSERT_EQ(contacts.size(), 1u);
  expectNormal(contacts[0], up());
  EXPECT_NEAR(contacts[0].overlap(), 0.1f, tightTolerance);
}

TEST(TerrainContact, AHugeSweepStillAnswersWithoutWalkingEveryChunkItCovers)
{
  World world(flatGround());
  const glm::vec3 everywhere(1e9f);

  EXPECT_EQ(contactsOf(world, *world.sphereAt({ centerX, 0.8f, centerZ }, 1.0f), everywhere).size(), 1u);
  EXPECT_TRUE(contactsOf(world, *world.sphereAt({ centerX, 20.0f, centerZ }, 1.0f), everywhere).empty());
}

TEST(TerrainContact, ASphereOverAChunkBorderMatchesOneInTheMiddleOfAChunk)
{
  World world(flatGround());

  const auto middle = contactsOf(world, *world.sphereAt({ 47.5f, 0.8f, centerZ }, 1.0f));
  const auto border = contactsOf(world, *world.sphereAt({ 32.0f, 0.8f, 32.0f }, 1.0f));

  ASSERT_EQ(middle.size(), 1u);
  ASSERT_EQ(border.size(), 1u);
  expectNormal(border[0], up());
  EXPECT_NEAR(border[0].overlap(), middle[0].overlap(), 1e-3f);
  EXPECT_NEAR(border[0].overlap(), 0.2f, tolerance);
}

TEST(TerrainContact, ACapsuleLyingOnFlatGroundRestsOnBothEnds)
{
  World world(flatGround());
  const auto capsule = world.capsuleAt({ centerX, 0.45f, centerZ }, 0.5f, 3.0f, { 0, 0, 90 });

  const auto contacts = contactsOf(world, *capsule);

  ASSERT_EQ(contacts.size(), 1u);
  expectNormal(contacts[0], up());
  ASSERT_EQ(contacts[0].contactPoints().size(), 2u);
  EXPECT_NEAR(contacts[0].depthAt(0), 0.05f, tolerance);
  EXPECT_NEAR(contacts[0].depthAt(1), contacts[0].depthAt(0), 1e-3f);
  EXPECT_NEAR(contacts[0].overlap(), 0.05f, tolerance);

  const auto [low, high] = std::minmax(contacts[0].contactPoints()[0].x, contacts[0].contactPoints()[1].x);
  EXPECT_NEAR(low, centerX - 1.0f, tolerance);
  EXPECT_NEAR(high, centerX + 1.0f, tolerance);
  EXPECT_NEAR(contacts[0].contactPoints()[0].y, 0.0f, tolerance);
}

TEST(TerrainContact, AnUprightCapsuleTouchesAtOnePointUnderItsBottomCap)
{
  World world(flatGround());
  const auto capsule = world.capsuleAt({ centerX, 0.95f, centerZ }, 0.5f, 2.0f);

  const auto contacts = contactsOf(world, *capsule);

  ASSERT_EQ(contacts.size(), 1u);
  expectNormal(contacts[0], up());
  ASSERT_EQ(contacts[0].contactPoints().size(), 1u);
  EXPECT_NEAR(contacts[0].overlap(), 0.05f, tolerance);
  fixtures::expectNear("point", contacts[0].point, { centerX, 0.0f, centerZ }, tolerance);

  const auto airborne = world.capsuleAt({ centerX, 1.5f, centerZ }, 0.5f, 2.0f);
  EXPECT_TRUE(contactsOf(world, *airborne).empty());
}

TEST(TerrainContact, ACapsuleLyingAcrossASlopeGetsTheSlopeNormal)
{
  World world(slopedGround());
  const glm::vec3 onSlope(centerX, centerX * std::tan(glm::radians(20.0f)), centerZ);
  const auto capsule = world.capsuleAt(onSlope + slopeNormal() * 0.45f, 0.5f, 3.0f, { 90, 0, 0 });

  const auto contacts = contactsOf(world, *capsule);

  ASSERT_EQ(contacts.size(), 1u);
  EXPECT_LT(degreesBetween(contacts[0].normal(), slopeNormal()), 1.0f);
  EXPECT_NEAR(contacts[0].overlap(), 0.05f, tolerance);
  EXPECT_EQ(contacts[0].contactPoints().size(), 2u);
}

TEST(TerrainContact, IntersectsAndApproachesFollowTheDepthAndTheSweep)
{
  World world(flatGround());

  const auto sunk = world.sphereAt({ centerX, 0.8f, centerZ }, 1.0f);
  const auto hovering = world.sphereAt({ centerX, 1.05f, centerZ }, 1.0f);
  const auto high = world.sphereAt({ centerX, 1.3f, centerZ }, 1.0f);

  EXPECT_TRUE(collisions::intersectsTerrain(*sunk, *world.terrain));
  EXPECT_FALSE(collisions::intersectsTerrain(*hovering, *world.terrain));

  EXPECT_TRUE(collisions::approachesTerrain(*high, *world.terrain, { 0, -0.5f, 0 }));
  EXPECT_FALSE(collisions::approachesTerrain(*high, *world.terrain, glm::vec3(0)));
}

TEST(TerrainContact, ABodyMovingAwayFromTheGroundGetsNoSpeculativeContact)
{
  World world(flatGround());
  const auto hovering = world.sphereAt({ centerX, 1.3f, centerZ }, 1.0f);

  EXPECT_EQ(contactsOf(world, *hovering, { 0, -0.5f, 0 }).size(), 1u);
  EXPECT_TRUE(collisions::approachesTerrain(*hovering, *world.terrain, { 0, -0.5f, 0 }));

  EXPECT_TRUE(contactsOf(world, *hovering, { 0, 0.5f, 0 }).empty());
  EXPECT_FALSE(collisions::approachesTerrain(*hovering, *world.terrain, { 0, 0.5f, 0 }));
}

TEST(TerrainContact, ATerrainAsTheCollidingColliderGivesNothing)
{
  World world(flatGround());
  const glm::vec3 spot(centerX, 0.4f, centerZ);

  EXPECT_EQ(contactsOf(world, *world.sphereAt(spot, 1.0f)).size(), 1u);

  EXPECT_TRUE(contactsOf(world, *world.terrain).empty());
  EXPECT_FALSE(collisions::intersectsTerrain(*world.terrain, *world.terrain));
}

TEST(TerrainContact, AHullWithNoUsableDataIsRefusedAndTheColliderKeepsItsCube)
{
  World world(flatGround());
  const auto hull = world.hullAt({ centerX, 0.98f, centerZ }, octahedron());

  EXPECT_FALSE(hull->setHull(ConvexHull{}));
  EXPECT_EQ(hull->getHull().vertices.size(), 6u);
  EXPECT_EQ(contactsOf(world, *hull).size(), 1u);

  const auto far = world.hullAt({ centerX, 20.0f, centerZ }, octahedron());
  EXPECT_TRUE(contactsOf(world, *far).empty());
}

TEST(TerrainContact, ATerrainWithCollisionOffOrNoVolumeGivesNothing)
{
  World world(flatGround());
  const auto sphere = world.sphereAt({ centerX, 0.8f, centerZ }, 1.0f);

  EXPECT_EQ(contactsOf(world, *sphere).size(), 1u);
  EXPECT_TRUE(collisions::intersectsTerrain(*sphere, *world.terrain));

  world.terrain->setCollisionEnabled(false);
  EXPECT_TRUE(contactsOf(world, *sphere).empty());
  EXPECT_FALSE(collisions::intersectsTerrain(*sphere, *world.terrain));
  EXPECT_FALSE(collisions::approachesTerrain(*sphere, *world.terrain, { 0, -1, 0 }));

  world.terrain->setCollisionEnabled(true);
  EXPECT_EQ(contactsOf(world, *sphere).size(), 1u);

  world.terrain->setVolume(std::weak_ptr<const terrain::TerrainVolume>());
  EXPECT_TRUE(contactsOf(world, *sphere).empty());
}

namespace {
  void expectCornersOfFlatBox(const collisions::Contact& contact, const float x, const float y, const float z,
                              const float half)
  {
    ASSERT_EQ(contact.pointCount, 4u);

    for (const float dx : { -half, half })
    {
      for (const float dz : { -half, half })
      {
        const glm::vec3 corner(x + dx, y, z + dz);
        const auto points = contact.contactPoints();

        EXPECT_TRUE(std::ranges::any_of(points, [&corner](const glm::vec3& point)
        {
          return glm::distance(point, corner) < tolerance;
        })) << "no contact point at the corner " << corner.x << ", " << corner.z;
      }
    }
  }
}

TEST(TerrainContact, ABoxLyingFlatOnFlatGroundReportsOneContactOnItsFourBottomCorners)
{
  World world(flatGround());
  const auto box = world.boxAt({ centerX, 1.0f, centerZ }, glm::vec3(1));

  const auto contacts = contactsOf(world, *box);

  ASSERT_EQ(contacts.size(), 1u);
  expectNormal(contacts[0], up());
  expectCornersOfFlatBox(contacts[0], centerX, 0.0f, centerZ, 1.0f);
  for (size_t i = 0; i < contacts[0].pointCount; ++i)
  {
    EXPECT_NEAR(contacts[0].depthAt(i), 0.0f, tightTolerance);
  }

  const auto sunk = world.boxAt({ centerX + 10.0f, 0.95f, centerZ }, glm::vec3(1));
  const auto sunkContacts = contactsOf(world, *sunk);

  ASSERT_EQ(sunkContacts.size(), 1u);
  expectCornersOfFlatBox(sunkContacts[0], centerX + 10.0f, -0.05f, centerZ, 1.0f);
  EXPECT_NEAR(sunkContacts[0].overlap(), 0.05f, tightTolerance);
  for (size_t i = 0; i < sunkContacts[0].pointCount; ++i)
  {
    EXPECT_NEAR(sunkContacts[0].depthAt(i), 0.05f, tightTolerance);
  }
}

TEST(TerrainContact, ABoxTiltedOntoItsLowerEdgeReportsOneContactAlongThatEdge)
{
  World world(flatGround());
  const float root = std::sqrt(2.0f);
  const auto box = world.boxAt({ centerX, root - 0.02f, centerZ }, glm::vec3(1), { 0, 0, 45 });

  const auto contacts = contactsOf(world, *box);

  ASSERT_EQ(contacts.size(), 1u);
  expectNormal(contacts[0], up());
  ASSERT_GE(contacts[0].contactPoints().size(), 2u);
  for (const auto& point : contacts[0].contactPoints())
  {
    EXPECT_NEAR(point.x, centerX, tolerance);
    EXPECT_NEAR(point.y, -0.02f, tolerance);
    EXPECT_LE(std::abs(point.z - centerZ), 1.0f + tolerance);
  }

  float widest = 0.0f;
  for (const auto& point : contacts[0].contactPoints())
  {
    widest = std::max(widest, std::abs(point.z - centerZ));
  }
  EXPECT_NEAR(widest, 1.0f, tolerance);
}

TEST(TerrainContact, AWideBoxOverARidgeIsFoundByItsFacePointsWhereItsEdgesAndCornersMissIt)
{
  const float halfWidth = 10.0f;
  const float boxX = centerX - 0.5f;

  World ridge(roofGround(centerX));
  const auto box = ridge.boxAt({ boxX, 1.02f, centerZ }, { halfWidth, 1.0f, halfWidth });

  const auto contacts = contactsOf(ridge, *box);

  ASSERT_FALSE(contacts.empty());
  for (const auto& contact : contacts)
  {
    for (const auto& point : contact.contactPoints())
    {
      EXPECT_NEAR(point.x, centerX, tolerance);
      EXPECT_NEAR(point.y, 0.02f, tolerance);
      EXPECT_LT(std::abs(point.z - centerZ), halfWidth - 0.1f);
    }
  }

  // Positive control: with the apex under the box's edge, the vertices and edge points reach it.
  World edgeRidge(roofGround(centerX));
  const auto edgeBox = edgeRidge.boxAt({ centerX - halfWidth, 1.02f, centerZ }, { halfWidth, 1.0f, halfWidth });
  const auto edgeContacts = contactsOf(edgeRidge, *edgeBox);

  ASSERT_FALSE(edgeContacts.empty());
  for (const auto& point : edgeContacts[0].contactPoints())
  {
    EXPECT_NEAR(point.x, centerX, tolerance);
  }
}

TEST(TerrainContact, ABoxStraddlingAValleyTouchesBothWalls)
{
  World valley(valleyGround());
  const auto box = valley.boxAt({ centerX, 1.95f, centerZ }, glm::vec3(1));

  const auto contacts = contactsOf(valley, *box);

  ASSERT_EQ(contacts.size(), 2u);
  const float root = std::sqrt(0.5f);
  const glm::vec3 leftWall(root, root, 0.0f);
  const glm::vec3 rightWall(-root, root, 0.0f);
  const bool leftFirst = contacts[0].normal().x > 0.0f;
  EXPECT_LT(degreesBetween(contacts[leftFirst ? 0 : 1].normal(), leftWall), 15.0f);
  EXPECT_LT(degreesBetween(contacts[leftFirst ? 1 : 0].normal(), rightWall), 15.0f);
  EXPECT_NEAR(contacts[0].overlap(), 0.05f / std::sqrt(2.0f), tolerance);

  // Positive control: lifted clear of both walls it touches neither.
  const auto lifted = valley.boxAt({ centerX, 2.5f, centerZ }, glm::vec3(1));
  EXPECT_TRUE(contactsOf(valley, *lifted).empty());
}

TEST(TerrainContact, AHullStandingOnOneVertexTouchesTheGroundThere)
{
  World world(flatGround());
  const auto hull = world.hullAt({ centerX, 0.98f, centerZ }, octahedron());

  const auto contacts = contactsOf(world, *hull);

  ASSERT_EQ(contacts.size(), 1u);
  expectNormal(contacts[0], up());
  EXPECT_EQ(contacts[0].contactPoints().size(), 1u);
  fixtures::expectNear("point", contacts[0].point, { centerX, -0.02f, centerZ }, tolerance);
  EXPECT_NEAR(contacts[0].overlap(), 0.02f, tightTolerance);

  const auto high = world.hullAt({ centerX + 10.0f, 3.0f, centerZ }, octahedron());
  EXPECT_TRUE(contactsOf(world, *high).empty());
}

TEST(TerrainContact, ABoxFarAboveTheGroundGivesNothingUntilItIsLowered)
{
  World world(flatGround());

  EXPECT_TRUE(contactsOf(world, *world.boxAt({ centerX, 10.0f, centerZ }, glm::vec3(1))).empty());
  EXPECT_TRUE(contactsOf(world, *world.boxAt({ 200.0f, 1.0f, centerZ }, glm::vec3(1))).empty());
  EXPECT_TRUE(contactsOf(world, *world.boxAt({ centerX, 45.0f, centerZ }, glm::vec3(1))).empty());

  EXPECT_EQ(contactsOf(world, *world.boxAt({ centerX, 1.05f, centerZ }, glm::vec3(1))).size(), 1u);
}

TEST(TerrainContact, ABoxIntersectsAndApproachesTheGroundOnlyAsItShould)
{
  World world(flatGround());
  const auto sunk = world.boxAt({ centerX, 0.9f, centerZ }, glm::vec3(1));
  const auto hovering = world.boxAt({ centerX + 10.0f, 1.5f, centerZ }, glm::vec3(1));

  EXPECT_TRUE(collisions::intersectsTerrain(*sunk, *world.terrain));
  EXPECT_FALSE(collisions::intersectsTerrain(*hovering, *world.terrain));

  EXPECT_TRUE(collisions::approachesTerrain(*hovering, *world.terrain, { 0, -1, 0 }));
  EXPECT_FALSE(collisions::approachesTerrain(*hovering, *world.terrain, { 0, 1, 0 }));
  EXPECT_FALSE(collisions::approachesTerrain(*hovering, *world.terrain, glm::vec3(0)));

  world.terrain->setCollisionEnabled(false);
  EXPECT_FALSE(collisions::intersectsTerrain(*sunk, *world.terrain));
  EXPECT_TRUE(contactsOf(world, *sunk).empty());
}

TEST(TerrainContact, TheQueryPointsOfALargeBoxStayWithinTheCapAndKeepTheNearestFaceDense)
{
  World world(flatGround());
  const auto ground = collisions::detail::prepareGround(*world.terrain);
  ASSERT_TRUE(ground.has_value());
  terrain::Sampler sampler(*world.volume);

  const auto small = world.boxAt({ centerX, 1.0f, centerZ }, glm::vec3(1));
  EXPECT_EQ(collisions::detail::polytopeQueryPoints(*small, *ground, sampler).size(), 8u + 12u + 6u * 4u);

  const auto medium = world.boxAt({ centerX, 10.0f, centerZ }, glm::vec3(10));
  const auto mediumPoints = collisions::detail::polytopeQueryPoints(*medium, *ground, sampler);
  EXPECT_LE(mediumPoints.size(), collisions::detail::maxTerrainQueryPoints);
  const auto onBottomInterior = std::ranges::count_if(mediumPoints, [](const glm::vec3& point)
  {
    return std::abs(point.y) < tightTolerance && std::abs(point.x - centerX) < 9.9f &&
      std::abs(point.z - centerZ) < 9.9f;
  });
  EXPECT_EQ(onBottomInterior, 20 * 20);

  const auto huge = world.boxAt({ centerX, 30.0f, centerZ }, glm::vec3(30));
  const auto hugePoints = collisions::detail::polytopeQueryPoints(*huge, *ground, sampler);
  EXPECT_LE(hugePoints.size(), collisions::detail::maxTerrainQueryPoints);
  EXPECT_GT(hugePoints.size(), 8u);

  const auto contacts = contactsOf(world, *huge);

  ASSERT_EQ(contacts.size(), 1u);
  expectNormal(contacts[0], up());
  expectCornersOfFlatBox(contacts[0], centerX, 0.0f, centerZ, 30.0f);
}

TEST(TerrainContact, AFinelyTessellatedHullKeepsTheInteriorOfItsBottomFaceWhereTheGroupsOutnumberTheBudget)
{
  constexpr int sides = 120;
  constexpr float radius = 5.0f;
  std::vector<glm::vec3> points;
  for (int i = 0; i < sides; ++i)
  {
    const float angle = glm::radians(360.0f * static_cast<float>(i) / static_cast<float>(sides));
    for (const float y : { -1.0f, 1.0f })
    {
      points.emplace_back(radius * std::cos(angle), y, radius * std::sin(angle));
    }
  }

  const auto built = buildConvexHull(points);
  ASSERT_TRUE(built.has_value());

  World world(flatGround());
  const auto ground = collisions::detail::prepareGround(*world.terrain);
  ASSERT_TRUE(ground.has_value());
  terrain::Sampler sampler(*world.volume);
  const auto prism = world.hullAt({ centerX, 1.0f, centerZ }, *built);

  const auto queryPoints = collisions::detail::polytopeQueryPoints(*prism, *ground, sampler);

  EXPECT_LE(queryPoints.size(), collisions::detail::maxTerrainQueryPoints);
  const auto bottomInterior = std::ranges::count_if(queryPoints, [](const glm::vec3& point)
  {
    return std::abs(point.y) < tightTolerance && std::hypot(point.x - centerX, point.z - centerZ) < radius - 0.6f;
  });
  EXPECT_GE(bottomInterior, 50);

  const auto contacts = contactsOf(world, *prism);

  ASSERT_EQ(contacts.size(), 1u);
  expectNormal(contacts[0], up());
}

TEST(TerrainContact, ASphereSweptFarThroughAirReportsTheGroundItWillReach)
{
  World world(flatGround());
  const auto sphere = world.sphereAt({ centerX, 21.0f, centerZ }, 1.0f);

  const auto contacts = contactsOf(world, *sphere, { 0, -30, 0 });

  ASSERT_EQ(contacts.size(), 1u);
  expectNormal(contacts[0], up());
  EXPECT_NEAR(contacts[0].overlap(), -20.0f, tolerance);
  EXPECT_NEAR(contacts[0].advance, 30.0f, tolerance);
  EXPECT_NEAR(contacts[0].depth(), 10.0f, tolerance);
  // On the sphere's side of the ground, one radius below its center, so the arm about the center is as it will be.
  fixtures::expectNear("point", contacts[0].point, { centerX, 20.0f, centerZ }, 0.1f);

  // Positive control: a sweep of one voxel reaches nothing, as before.
  EXPECT_TRUE(contactsOf(world, *sphere, { 0, -1, 0 }).empty());
  EXPECT_FALSE(collisions::approachesTerrain(*sphere, *world.terrain, { 0, -1, 0 }));
  EXPECT_TRUE(collisions::approachesTerrain(*sphere, *world.terrain, { 0, -30, 0 }));
}

TEST(TerrainContact, ABoxSweptFarThroughAirReportsTheGroundItWillReach)
{
  World world(flatGround());
  const auto box = world.boxAt({ centerX, 21.0f, centerZ }, glm::vec3(1));

  const auto contacts = contactsOf(world, *box, { 0, -30, 0 });

  ASSERT_EQ(contacts.size(), 1u);
  expectNormal(contacts[0], up());
  EXPECT_NEAR(contacts[0].overlap(), -20.0f, tolerance);
  EXPECT_NEAR(contacts[0].advance, 30.0f, tolerance);
  EXPECT_NEAR(contacts[0].depth(), 10.0f, tolerance);
  ASSERT_GE(contacts[0].contactPoints().size(), 2u);
  EXPECT_NEAR(contacts[0].depthAt(0), -20.0f, tolerance);
  for (size_t i = 1; i < contacts[0].contactPoints().size(); ++i)
  {
    EXPECT_LE(contacts[0].depthAt(i), -20.0f + tolerance);
    EXPECT_GT(contacts[0].depthAt(i), -22.0f);
  }

  EXPECT_TRUE(contactsOf(world, *box, { 0, -1, 0 }).empty());
}

TEST(TerrainContact, ASweptBodyAimedAtAWallStopsAtItsFace)
{
  World world(wallGround());
  const auto sphere = world.sphereAt({ 10.0f, 16.0f, centerZ }, 1.0f);

  const auto contacts = contactsOf(world, *sphere, { 40, 0, 0 });

  ASSERT_EQ(contacts.size(), 1u);
  expectNormal(contacts[0], { -1, 0, 0 });
  EXPECT_NEAR(contacts[0].overlap(), -(centerX - 10.0f - 1.0f), tolerance);

  // Positive control: swept parallel to the wall it never reaches it.
  EXPECT_TRUE(contactsOf(world, *sphere, { 0, 0, 40 }).empty());
}

TEST(TerrainContact, ASphereAndABoxDeepInsideFlatGroundAreLiftedToTheSurface)
{
  World world(flatGround());
  const auto sphere = world.sphereAt({ centerX, -6.0f, centerZ }, 1.0f);
  const auto box = world.boxAt({ centerX + 10.0f, -6.0f, centerZ }, glm::vec3(1));

  const auto sphereContacts = contactsOf(world, *sphere);
  ASSERT_EQ(sphereContacts.size(), 1u);
  expectNormal(sphereContacts[0], up());
  EXPECT_NEAR(sphereContacts[0].overlap(), 7.0f, tolerance);

  const auto boxContacts = contactsOf(world, *box);
  ASSERT_EQ(boxContacts.size(), 1u);
  expectNormal(boxContacts[0], up());
  EXPECT_NEAR(boxContacts[0].overlap(), 7.0f, tolerance);

  EXPECT_TRUE(collisions::intersectsTerrain(*sphere, *world.terrain));
  EXPECT_TRUE(collisions::intersectsTerrain(*box, *world.terrain));

  // Positive control: just inside the band the same shapes are found without any stepping.
  EXPECT_NEAR(contactsOf(world, *world.sphereAt({ centerX, 0.0f, centerZ }, 1.0f))[0].overlap(), 1.0f, tolerance);
}

TEST(TerrainContact, ADeepBodyStepsBackAlongItsSweepAndUpWhenItHasNone)
{
  World world(wallGround());
  const auto sphere = world.sphereAt({ centerX + 7.0f, 16.0f, centerZ }, 1.0f);
  const auto box = world.boxAt({ centerX + 7.0f, 16.0f, centerZ + 20.0f }, glm::vec3(1));

  // Without a sweep it steps up, and the wall's whole column is ground, so the sphere finds no way out.
  EXPECT_TRUE(contactsOf(world, *sphere).empty());

  const auto sphereContacts = contactsOf(world, *sphere, { 1, 0, 0 });
  ASSERT_EQ(sphereContacts.size(), 1u);
  expectNormal(sphereContacts[0], { -1, 0, 0 });
  EXPECT_NEAR(sphereContacts[0].overlap(), 8.0f, tolerance);

  const auto boxContacts = contactsOf(world, *box, { 1, 0, 0 });
  ASSERT_EQ(boxContacts.size(), 1u);
  expectNormal(boxContacts[0], { -1, 0, 0 });
  EXPECT_NEAR(boxContacts[0].overlap(), 8.0f, tolerance);
}
