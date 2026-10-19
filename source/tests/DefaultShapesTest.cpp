#include <gtest/gtest.h>

#include "DefaultShapes.h"
#include "objects/components/collisions/ConvexHullValidation.h"

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
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {
  // The distinct POSITION values of the first mesh in a default asset's .glb.
  std::set<std::tuple<float, float, float>> glbPositions(const std::string& file)
  {
    const auto path = std::filesystem::path(ECS3D_SOURCE_DIR) / "source" / "apps" / "server" / "defaultAssets"
      / "models" / file;
    std::ifstream stream(path, std::ios::binary);
    EXPECT_TRUE(stream.is_open()) << "cannot open " << path.string();
    const std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

    uint32_t jsonLength = 0;
    std::memcpy(&jsonLength, bytes.data() + 12, sizeof(jsonLength));
    const auto gltf = nlohmann::json::parse(bytes.begin() + 20, bytes.begin() + 20 + jsonLength);

    const auto& primitive = gltf.at("meshes").at(0).at("primitives").at(0);
    const auto& accessor = gltf.at("accessors").at(primitive.at("attributes").at("POSITION").get<size_t>());
    const auto& view = gltf.at("bufferViews").at(accessor.at("bufferView").get<size_t>());
    const size_t binaryStart = 20 + jsonLength + 8;
    const size_t offset = binaryStart + view.value("byteOffset", 0) + accessor.value("byteOffset", 0);

    std::set<std::tuple<float, float, float>> positions;
    for (size_t i = 0; i < accessor.at("count").get<size_t>(); ++i)
    {
      float v[3];
      std::memcpy(v, bytes.data() + offset + i * sizeof(v), sizeof(v));
      positions.emplace(std::round(v[0] * 1e4f) / 1e4f, std::round(v[1] * 1e4f) / 1e4f,
                        std::round(v[2] * 1e4f) / 1e4f);
    }

    return positions;
  }

  bool hasVertexNear(const ConvexHull& hull, const std::tuple<float, float, float>& point)
  {
    const glm::vec3 p(std::get<0>(point), std::get<1>(point), std::get<2>(point));

    return std::ranges::any_of(hull.vertices, [&](const glm::vec3& v) { return glm::length(v - p) < 1e-3f; });
  }

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
  EXPECT_EQ(corners.size(), 6u);
  for (const auto& corner : corners)
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
  size_t ringVertices = 0;
  for (const auto& position : glbPositions("cylinder.glb"))
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
