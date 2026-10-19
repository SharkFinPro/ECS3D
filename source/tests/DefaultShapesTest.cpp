#include <gtest/gtest.h>

#include "DefaultShapes.h"
#include "TestScene.h"
#include "collisions/SupportFaces.h"
#include "objects/Object.h"
#include "objects/components/collisions/ConvexHullValidation.h"
#include "objects/components/collisions/ConvexMeshCollider.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {
  // The distinct POSITION values of the first mesh in a default asset's .glb.
  std::optional<std::set<std::tuple<float, float, float>>> glbPositions(const std::string& file)
  {
    const auto path = std::filesystem::path(ECS3D_SOURCE_DIR) / "source" / "apps" / "server" / "defaultAssets"
      / "models" / file;
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
    {
      return std::nullopt;
    }
    const std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

    if (bytes.size() < 20)
    {
      return std::nullopt;
    }

    uint32_t jsonLength = 0;
    std::memcpy(&jsonLength, bytes.data() + 12, sizeof(jsonLength));
    if (bytes.size() < 28 + static_cast<size_t>(jsonLength))
    {
      return std::nullopt;
    }
    const auto gltf = nlohmann::json::parse(bytes.begin() + 20, bytes.begin() + 20 + jsonLength);

    const auto& primitive = gltf.at("meshes").at(0).at("primitives").at(0);
    const auto& accessor = gltf.at("accessors").at(primitive.at("attributes").at("POSITION").get<size_t>());
    const auto& view = gltf.at("bufferViews").at(accessor.at("bufferView").get<size_t>());
    const size_t binaryStart = 20 + jsonLength + 8;
    const size_t offset = binaryStart + view.value("byteOffset", 0) + accessor.value("byteOffset", 0);

    const size_t count = accessor.at("count").get<size_t>();
    if (bytes.size() < offset + count * 3 * sizeof(float))
    {
      return std::nullopt;
    }

    std::set<std::tuple<float, float, float>> positions;
    for (size_t i = 0; i < count; ++i)
    {
      float v[3];
      std::memcpy(v, bytes.data() + offset + i * sizeof(v), sizeof(v));
      positions.emplace(std::round(v[0] * 1e4f) / 1e4f, std::round(v[1] * 1e4f) / 1e4f,
                        std::round(v[2] * 1e4f) / 1e4f);
    }

    return positions;
  }

  bool hasVertexNear(const ConvexHull& hull, const glm::vec3& p, const float tolerance = 1e-3f)
  {
    return std::ranges::any_of(hull.vertices, [&](const glm::vec3& v) { return glm::length(v - p) < tolerance; });
  }

  bool hasVertexNear(const ConvexHull& hull, const std::tuple<float, float, float>& point)
  {
    return hasVertexNear(hull, glm::vec3(std::get<0>(point), std::get<1>(point), std::get<2>(point)));
  }

  // Faces a body can rest on, with coplanar hull triangles merged.
  size_t restingFaceCount(const ConvexHull& hull)
  {
    const auto scene = fixtures::makeScene();
    const auto object = fixtures::addObject(scene, "Shape");
    const auto collider = std::make_shared<ConvexMeshCollider>();
    EXPECT_TRUE(collider->setHull(hull));
    object->addComponent(collider);

    return collisions::supportFacesOf(*collider).size();
  }

  struct ShapeSpec
  {
    const char* name;
    ConvexHull (*build)();
    const char* model;
    size_t vertices;
    size_t faces;
    // Zero for a polyhedron that only has to fit the -1..1 cube.
    float radius;
    float halfHeight;
  };

  const std::vector<ShapeSpec> shapes = {
    { "D4", d4Hull, "d4.glb", 4, 4, 0.0f, 0.0f },
    { "D6", d6Hull, nullptr, 8, 6, 0.0f, 0.0f },
    { "D8", d8Hull, "d8.glb", 6, 8, 0.0f, 0.0f },
    { "D12", d12Hull, "d12.glb", 20, 12, 0.0f, 0.0f },
    { "D20", d20Hull, "d20.glb", 12, 20, 0.0f, 0.0f },
    { "HexPrism", hexPrismHull, "hex_prism.glb", 12, 8, 0.6f, 1.0f },
    { "Drum", drumHull, "drum.glb", 32, 18, 1.0f, 0.45f },
    { "Gem", gemHull, "gem.glb", 17, 17, 0.0f, 0.0f },
    { "Boulder", boulderHull, "boulder.glb", 24, 44, 0.0f, 0.0f }
  };

  // Closed (every directed edge has its reverse), outward counter-clockwise, and no vertex above any face.
  void expectClosedConvexOutward(const ConvexHull& hull)
  {
    ASSERT_TRUE(isValidConvexHull(hull));

    glm::vec3 center(0.0f);
    for (const auto& v : hull.vertices)
    {
      center += v;
    }
    center /= static_cast<float>(hull.vertices.size());

    std::map<std::pair<uint32_t, uint32_t>, int> edges;
    for (size_t t = 0; t < hull.indices.size(); t += 3)
    {
      const uint32_t ids[3] = { hull.indices[t], hull.indices[t + 1], hull.indices[t + 2] };
      for (int e = 0; e < 3; ++e)
      {
        ++edges[{ ids[e], ids[(e + 1) % 3] }];
      }

      const auto& a = hull.vertices[ids[0]];
      const auto normal = glm::cross(hull.vertices[ids[1]] - a, hull.vertices[ids[2]] - a);
      ASSERT_GT(glm::length(normal), 1e-6f);
      EXPECT_GT(glm::dot(normal, a - center), 0.0f) << "triangle " << t / 3 << " faces inward";

      for (const auto& v : hull.vertices)
      {
        EXPECT_LE(glm::dot(glm::normalize(normal), v - a), 1e-4f) << "triangle " << t / 3 << " is not a face";
      }
    }

    for (const auto& [edge, count] : edges)
    {
      EXPECT_EQ(count, 1);
      EXPECT_EQ(edges.count({ edge.second, edge.first }), 1u);
    }
  }
}

TEST(DefaultShapes, TheRampHullIsTheSixCornersOfTheWedge)
{
  const auto hull = rampHull();

  EXPECT_EQ(hull.vertices.size(), 6u);
  expectClosedConvexOutward(hull);
  EXPECT_NEAR(hullVolume(hull), 4.0f, 1e-4f);

  const auto corners = glbPositions("ramp.glb");
  ASSERT_TRUE(corners.has_value());
  EXPECT_EQ(corners->size(), 6u);
  for (const auto& corner : *corners)
  {
    EXPECT_TRUE(hasVertexNear(hull, corner));
  }
}

TEST(DefaultShapes, TheCylinderHullMatchesTheRingsOfCylinderGlb)
{
  const auto hull = cylinderHull();

  EXPECT_EQ(hull.vertices.size(), 64u);
  expectClosedConvexOutward(hull);
  EXPECT_NEAR(hullVolume(hull), 32.0f * std::sin(std::numbers::pi_v<float> / 16.0f), 1e-3f);

  // The glb also holds each cap's center; the ring vertices are the ones at radius one.
  const auto positions = glbPositions("cylinder.glb");
  ASSERT_TRUE(positions.has_value());
  size_t ringVertices = 0;
  for (const auto& position : *positions)
  {
    const float radius = std::hypot(std::get<0>(position), std::get<2>(position));
    if (radius < 0.5f)
    {
      continue;
    }

    ++ringVertices;
    EXPECT_TRUE(hasVertexNear(hull, position));
  }

  EXPECT_EQ(ringVertices, hull.vertices.size());
}

TEST(DefaultShapes, TheColliderBlobRecordsTheHullAndItsSourceModel)
{
  const auto hull = rampHull();
  const auto collider = convexMeshCollider(hull, "e77194e0-e50e-4070-aacd-2f5d917b2da7");

  EXPECT_EQ(collider.at("subType"), "ConvexMesh");
  EXPECT_EQ(collider.at("sourceModel"), "e77194e0-e50e-4070-aacd-2f5d917b2da7");
  EXPECT_EQ(collider.at("vertices").size(), hull.vertices.size() * 3);
  EXPECT_EQ(collider.at("indices").size(), hull.indices.size());
  EXPECT_EQ(collider.at("vertices").at(3).get<float>(), hull.vertices.at(1).x);
}

TEST(DefaultShapes, EveryLibraryHullIsClosedConvexWithItsVertexAndFaceCounts)
{
  for (const auto& spec : shapes)
  {
    SCOPED_TRACE(spec.name);
    const auto hull = spec.build();

    expectClosedConvexOutward(hull);
    EXPECT_EQ(hull.vertices.size(), spec.vertices);
    EXPECT_LE(hull.vertices.size(), 64u);
    EXPECT_GT(hullVolume(hull), 0.1f);
    EXPECT_EQ(restingFaceCount(hull), spec.faces);
  }

  // Positive control: the face count merges triangles, so a hull with more triangles than faces exists here.
  const auto d12 = d12Hull();
  EXPECT_EQ(d12.indices.size() / 3, 36u);
  EXPECT_EQ(restingFaceCount(d12), 12u);
}

TEST(DefaultShapes, EveryLibraryHullFitsItsBounds)
{
  constexpr float tolerance = 1e-4f;

  for (const auto& spec : shapes)
  {
    SCOPED_TRACE(spec.name);
    const auto hull = spec.build();

    float reachRadius = 0.0f;
    float reachHeight = 0.0f;
    for (const auto& v : hull.vertices)
    {
      EXPECT_LE(std::max({ std::abs(v.x), std::abs(v.y), std::abs(v.z) }), 1.0f + tolerance);
      reachRadius = std::max(reachRadius, std::hypot(v.x, v.z));
      reachHeight = std::max(reachHeight, std::abs(v.y));
    }

    if (spec.radius > 0.0f)
    {
      EXPECT_NEAR(reachRadius, spec.radius, tolerance);
      EXPECT_NEAR(reachHeight, spec.halfHeight, tolerance);
    }
  }
}

TEST(DefaultShapes, EveryLibraryHullIsTheVerticesOfItsModel)
{
  size_t compared = 0;
  for (const auto& spec : shapes)
  {
    if (spec.model == nullptr)
    {
      continue;
    }

    SCOPED_TRACE(spec.name);
    const auto hull = spec.build();
    const auto positions = glbPositions(spec.model);
    ASSERT_TRUE(positions.has_value());

    EXPECT_EQ(positions->size(), hull.vertices.size());
    for (const auto& position : *positions)
    {
      EXPECT_TRUE(hasVertexNear(hull, position));
    }

    ++compared;
  }

  EXPECT_EQ(compared, shapes.size() - 1);
}

TEST(DefaultShapes, TheModelsAreUnderTenKilobytes)
{
  const auto models = std::filesystem::path(ECS3D_SOURCE_DIR) / "source" / "apps" / "server" / "defaultAssets"
    / "models";

  for (const auto& spec : shapes)
  {
    if (spec.model != nullptr)
    {
      SCOPED_TRACE(spec.model);
      EXPECT_LT(std::filesystem::file_size(models / spec.model), 10u * 1024u);
    }
  }
}

TEST(DefaultShapes, TheBoulderPointsMatchTheGeneratorInToolsShapes)
{
  const auto points = boulderPoints();
  ASSERT_EQ(points.size(), 24u);

  // Printed by tools/shapes/library.py; a change to either side's generator breaks this.
  EXPECT_NEAR(points[0].x, 0.3423016f, 1e-5f);
  EXPECT_NEAR(points[0].y, 0.5538972f, 1e-5f);
  EXPECT_NEAR(points[0].z, 0.0538009f, 1e-5f);
  EXPECT_NEAR(points[7].x, -0.3585087f, 1e-5f);
  EXPECT_NEAR(points[7].y, 0.1679853f, 1e-5f);
  EXPECT_NEAR(points[7].z, -0.8827560f, 1e-5f);

  // Squashed along Y, with radii between the minimum and one.
  float tallest = 0.0f;
  float widest = 0.0f;
  for (const auto& p : points)
  {
    tallest = std::max(tallest, std::abs(p.y));
    widest = std::max(widest, std::hypot(p.x, p.z));
  }

  EXPECT_LE(tallest, 0.7f + 1e-4f);
  EXPECT_GT(widest, 0.75f);
  EXPECT_LE(widest, 1.0f + 1e-4f);
}

TEST(DefaultShapes, TheGemHasThePointTheGirdleAndTheTableOfItsGenerator)
{
  const auto hull = gemHull();

  // A table corner, printed by tools/shapes/library.py.
  EXPECT_TRUE(hasVertexNear(hull, { -0.3889087f, 0.55f, 0.3889087f }, 1e-5f));
  EXPECT_TRUE(hasVertexNear(hull, { 0.0f, -1.0f, 0.0f }, 1e-5f));
  EXPECT_TRUE(hasVertexNear(hull, { 1.0f, 0.1f, 0.0f }, 1e-5f));

  // Positive control for the lookup.
  EXPECT_FALSE(hasVertexNear(hull, { 0.3f, 0.3f, 0.3f }, 1e-5f));
}
