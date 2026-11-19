#include <gtest/gtest.h>

#include "TestScene.h"
#include "collisions/TerrainContact.h"
#include "objects/Object.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/CapsuleCollider.h"
#include "objects/components/collisions/ConvexMeshCollider.h"
#include "objects/components/collisions/SphereCollider.h"
#include "objects/components/collisions/Terrain.h"
#include "terrain/TerrainChunk.h"
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
  };

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

TEST(TerrainContact, OnlySpheresAndCapsulesAreHandled)
{
  World world(flatGround());
  const glm::vec3 spot(centerX, 0.4f, centerZ);

  EXPECT_EQ(contactsOf(world, *world.sphereAt(spot, 1.0f)).size(), 1u);

  const auto boxObject = fixtures::addObject(world.scene, "Box", world.worldOf(spot));
  const auto box = fixtures::addBoxCollider(boxObject);
  EXPECT_TRUE(contactsOf(world, *box).empty());
  EXPECT_FALSE(collisions::intersectsTerrain(*box, *world.terrain));
  EXPECT_FALSE(collisions::approachesTerrain(*box, *world.terrain, { 0, -1, 0 }));

  const auto hullObject = fixtures::addObject(world.scene, "Hull", world.worldOf(spot));
  const auto hull = std::make_shared<ConvexMeshCollider>();
  hullObject->addComponent(hull);
  EXPECT_TRUE(contactsOf(world, *hull).empty());

  EXPECT_TRUE(contactsOf(world, *world.terrain).empty());
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
