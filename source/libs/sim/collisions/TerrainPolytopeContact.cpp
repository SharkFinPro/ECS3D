#include "TerrainContactInternal.h"
#include "FaceManifold.h"
#include "SupportFaces.h"
#include "TerrainContact.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/Collider.h"
#include "objects/components/collisions/ConvexMeshCollider.h"
#include "objects/components/collisions/Terrain.h"
#include "objects/components/collisions/WorldFace.h"
#include "terrain/TerrainSampling.h"
#include "terrain/TerrainVolume.h"
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace collisions::detail {
  namespace {
    constexpr size_t polytopeManifoldPoints = 4;

    // A count of segments or cells is rounded up only past this much of one, so a 20 unit edge over 1 unit voxels
    // is 20 segments whatever the last bit of its length says.
    constexpr float spanSlack = 1e-3f;

    constexpr float maxCellsPerAxis = 1048576.0f;

    constexpr float spacingGrowth = 1.25f;

    constexpr int spacingLevels = 64;

    // Raw field distances are ranked at this resolution, in hundredths of a voxel.
    constexpr float rankQuantum = 100.0f;

    using Edge = std::pair<glm::vec3, glm::vec3>;

    struct Geometry {
      std::vector<glm::vec3> vertices;
      std::vector<Edge> edges;
      std::vector<WorldFace> ownedFaces;
      const std::vector<WorldFace>* sharedFaces = nullptr;

      [[nodiscard]] std::span<const WorldFace> faces() const
      {
        return sharedFaces != nullptr ? std::span<const WorldFace>(*sharedFaces)
                                      : std::span<const WorldFace>(ownedFaces);
      }
    };

    struct FaceGrid {
      glm::vec3 base;
      glm::vec3 u;
      glm::vec3 v;
      float minU;
      float minV;
      float stepU;
      float stepV;
      int cellsU;
      int cellsV;
    };

    // An edge or a face whose points are made at a spacing, ranked by how near the ground its middle is.
    struct Group {
      const Edge* edge;
      const WorldFace* face;
      float rank;
    };

    [[nodiscard]] bool before(const glm::vec3& a, const glm::vec3& b)
    {
      if (a.x != b.x)
      {
        return a.x < b.x;
      }

      return a.y != b.y ? a.y < b.y : a.z < b.z;
    }

    [[nodiscard]] std::vector<glm::vec3> boxCorners(BoxCollider& box)
    {
      const auto& frame = box.getWorldFrame();

      std::vector<glm::vec3> corners;
      for (int mask = 0; mask < 8; ++mask)
      {
        glm::vec3 corner = frame.center;
        for (size_t axis = 0; axis < 3; ++axis)
        {
          const float sign = (mask & (1 << axis)) != 0 ? 1.0f : -1.0f;
          corner += frame.axes[axis] * (sign * frame.halfExtents[axis]);
        }

        corners.push_back(corner);
      }

      return corners;
    }

    [[nodiscard]] Geometry boxGeometry(BoxCollider& box)
    {
      Geometry geometry;
      geometry.vertices = boxCorners(box);

      for (int mask = 0; mask < 8; ++mask)
      {
        for (int axis = 0; axis < 3; ++axis)
        {
          if ((mask & (1 << axis)) == 0)
          {
            geometry.edges.emplace_back(geometry.vertices[static_cast<size_t>(mask)],
                                        geometry.vertices[static_cast<size_t>(mask | (1 << axis))]);
          }
        }
      }

      geometry.ownedFaces = supportFacesOf(box);

      return geometry;
    }

    [[nodiscard]] Geometry hullGeometry(ConvexMeshCollider& hull)
    {
      Geometry geometry;
      geometry.vertices = hull.getWorldVertices();
      geometry.sharedFaces = &hullSupportFaces(hull);

      // Each edge borders two faces and runs opposite ways in them, so it is kept where it runs "forward".
      for (const auto& face : geometry.faces())
      {
        for (size_t i = 0; i < face.corners.size(); ++i)
        {
          const auto& from = face.corners[i];
          const auto& to = face.corners[(i + 1) % face.corners.size()];
          if (face.corners.size() >= 3 && before(from, to))
          {
            geometry.edges.emplace_back(from, to);
          }
        }
      }

      return geometry;
    }

    [[nodiscard]] Geometry geometryOf(Collider& collider)
    {
      if (collider.getColliderType() == ColliderType::boxCollider)
      {
        return boxGeometry(static_cast<BoxCollider&>(collider));
      }

      return hullGeometry(static_cast<ConvexMeshCollider&>(collider));
    }

    [[nodiscard]] int cellsAlong(const float extent, const float spacing)
    {
      return std::max(1, static_cast<int>(std::min(std::ceil(extent / spacing - spanSlack), maxCellsPerAxis)));
    }

    [[nodiscard]] FaceGrid gridOf(const WorldFace& face, const float spacing)
    {
      const auto reference = std::abs(face.normal.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
      FaceGrid grid{ face.corners.front(), glm::normalize(glm::cross(face.normal, reference)), glm::vec3(0), 0, 0, 0,
                     0, 1, 1 };
      grid.v = glm::cross(face.normal, grid.u);

      float maxU = 0.0f;
      float maxV = 0.0f;
      for (const auto& corner : face.corners)
      {
        const float along = glm::dot(corner - grid.base, grid.u);
        const float across = glm::dot(corner - grid.base, grid.v);
        grid.minU = std::min(grid.minU, along);
        grid.minV = std::min(grid.minV, across);
        maxU = std::max(maxU, along);
        maxV = std::max(maxV, across);
      }

      grid.cellsU = cellsAlong(maxU - grid.minU, spacing);
      grid.cellsV = cellsAlong(maxV - grid.minV, spacing);
      grid.stepU = (maxU - grid.minU) / static_cast<float>(grid.cellsU);
      grid.stepV = (maxV - grid.minV) / static_cast<float>(grid.cellsV);

      return grid;
    }

    [[nodiscard]] bool insideFace(const WorldFace& face, const glm::vec3& point)
    {
      for (size_t i = 0; i < face.corners.size(); ++i)
      {
        const auto& from = face.corners[i];
        const auto& to = face.corners[(i + 1) % face.corners.size()];
        if (glm::dot(glm::cross(to - from, point - from), face.normal) < 0.0f)
        {
          return false;
        }
      }

      return true;
    }

    // An upper bound on the points the group gives at this spacing, without making them.
    [[nodiscard]] size_t pointBound(const Group& group, const float spacing)
    {
      if (group.edge != nullptr)
      {
        return static_cast<size_t>(cellsAlong(glm::distance(group.edge->first, group.edge->second), spacing) - 1);
      }

      const auto grid = gridOf(*group.face, spacing);

      return static_cast<size_t>(grid.cellsU) * static_cast<size_t>(grid.cellsV);
    }

    void appendPoints(const Group& group, const float spacing, std::vector<glm::vec3>& points)
    {
      if (group.edge != nullptr)
      {
        const int segments = cellsAlong(glm::distance(group.edge->first, group.edge->second), spacing);
        for (int i = 1; i < segments; ++i)
        {
          points.push_back(glm::mix(group.edge->first, group.edge->second,
                                    static_cast<float>(i) / static_cast<float>(segments)));
        }

        return;
      }

      const auto& face = *group.face;
      const auto grid = gridOf(face, spacing);
      for (int j = 0; j < grid.cellsV; ++j)
      {
        for (int i = 0; i < grid.cellsU; ++i)
        {
          const auto point = grid.base + grid.u * (grid.minU + (static_cast<float>(i) + 0.5f) * grid.stepU) +
            grid.v * (grid.minV + (static_cast<float>(j) + 0.5f) * grid.stepV);
          if (insideFace(face, point))
          {
            points.push_back(point);
          }
        }
      }
    }

    [[nodiscard]] std::vector<Group> groupsOf(const Geometry& geometry, const TerrainGround& ground,
                                              terrain::Sampler& sampler, const glm::vec3& offset)
    {
      const auto rankAt = [&ground, &sampler, &offset](const glm::vec3& world)
      {
        return std::round(sampler.distance(ground.frame.toLocal(world + offset)) * rankQuantum);
      };

      std::vector<Group> groups;
      for (const auto& edge : geometry.edges)
      {
        groups.push_back({ &edge, nullptr, rankAt(0.5f * (edge.first + edge.second)) });
      }

      for (const auto& face : geometry.faces())
      {
        if (face.corners.size() >= 3)
        {
          groups.push_back({ nullptr, &face, rankAt(face.center) });
        }
      }

      return groups;
    }

    // The smallest spacing on a ladder of 25 percent steps at which the group gives no more than allowance
    // points, or a negative one when even its coarsest spacing does not.
    [[nodiscard]] float spacingFor(const Group& group, const float voxelSize, const size_t allowance)
    {
      float spacing = voxelSize;
      for (int level = 0; level < spacingLevels; ++level, spacing *= spacingGrowth)
      {
        if (pointBound(group, spacing) <= allowance)
        {
          return spacing;
        }
      }

      return -1.0f;
    }

    [[nodiscard]] std::vector<glm::vec3> cappedPoints(const Geometry& geometry, const std::vector<Group>& groups,
                                                      const float voxelSize)
    {
      const size_t budget = geometry.vertices.size() < maxTerrainQueryPoints
                              ? maxTerrainQueryPoints - geometry.vertices.size() : 0;

      size_t full = 0;
      for (const auto& group : groups)
      {
        full += pointBound(group, voxelSize);
      }

      std::vector<float> spacings(groups.size(), voxelSize);
      if (full > budget)
      {
        // The nearest groups first keep the density they can. Later groups are reserved a point each, but never more
        // than half of what is left, so the nearest group is never starved; groups past the budget get none.
        std::vector<size_t> order(groups.size());
        for (size_t i = 0; i < order.size(); ++i)
        {
          order[i] = i;
        }

        std::ranges::stable_sort(order, [&groups](const size_t a, const size_t b)
        {
          return groups[a].rank != groups[b].rank ? groups[a].rank < groups[b].rank
                                                  : groups[a].face != nullptr && groups[b].face == nullptr;
        });

        size_t used = 0;
        for (size_t rank = 0; rank < order.size(); ++rank)
        {
          const size_t later = order.size() - rank - 1;
          const size_t left = budget - used;
          const size_t allowance = left - std::min(later, left / 2);
          const size_t index = order[rank];

          spacings[index] = spacingFor(groups[index], voxelSize, allowance);
          if (spacings[index] > 0.0f)
          {
            used += pointBound(groups[index], spacings[index]);
          }
        }
      }

      std::vector<glm::vec3> points = geometry.vertices;
      for (size_t i = 0; i < groups.size(); ++i)
      {
        if (spacings[i] > 0.0f)
        {
          appendPoints(groups[i], spacings[i], points);
        }
      }

      return points;
    }

    // Measures each query point, taken offset along from where it is, against the ground that is near enough to touch
    // with what is left of the sweep. A point far in air has no usable gradient and is skipped; one deeper than the
    // band is stepped out of the ground against the sweep.
    [[nodiscard]] std::vector<TerrainCandidate> measuredPoints(Collider& collider, const TerrainGround& ground,
                                                               terrain::Sampler& sampler, const glm::vec3& sweep,
                                                               const glm::vec3& offset)
    {
      const float voxelSize = ground.volume->voxelSize();
      const float reject = contactMargin + glm::length(sweep) + voxelSize;
      const auto backward = stepBackDirection(ground.frame, sweep);

      std::vector<TerrainCandidate> candidates;
      for (const auto& placed : polytopeQueryPoints(collider, ground, sampler, offset))
      {
        const auto world = placed + offset;
        const auto local = ground.frame.toLocal(world);
        float raw = sampler.distance(local) * voxelSize;
        if (raw > reject)
        {
          continue;
        }

        auto surface = findSurface(sampler, local);
        if (!surface)
        {
          surface = findDeepSurface(sampler, local, backward, voxelSize);
          raw = unknownDepth;
        }

        if (surface)
        {
          auto candidate = measureCandidate(ground.frame, *surface, local, raw, 0.0f);
          candidate.point = world;
          candidates.push_back(candidate);
        }
      }

      return candidates;
    }

    // The center of the collider's bounds and the distance from it to its furthest vertex.
    [[nodiscard]] std::pair<glm::vec3, float> boundingSphereOf(Collider& collider)
    {
      const auto& bounds = collider.getBoundingBox();
      const glm::vec3 center(0.5f * (bounds.minX + bounds.maxX), 0.5f * (bounds.minY + bounds.maxY),
                             0.5f * (bounds.minZ + bounds.maxZ));

      const auto vertices = collider.getColliderType() == ColliderType::boxCollider
                              ? boxCorners(static_cast<BoxCollider&>(collider))
                              : static_cast<ConvexMeshCollider&>(collider).getWorldVertices();

      float radius = 0.0f;
      for (const auto& vertex : vertices)
      {
        radius = std::max(radius, glm::distance(center, vertex));
      }

      return { center, radius };
    }

    [[nodiscard]] std::optional<TerrainGround> groundWithin(Collider& collider, Terrain& terrain,
                                                            const glm::vec3& sweep)
    {
      auto ground = prepareGround(terrain);
      if (!ground)
      {
        return std::nullopt;
      }

      const auto& bounds = collider.getBoundingBox();
      const glm::vec3 low(bounds.minX + std::min(sweep.x, 0.0f), bounds.minY + std::min(sweep.y, 0.0f),
                          bounds.minZ + std::min(sweep.z, 0.0f));
      const glm::vec3 high(bounds.maxX + std::max(sweep.x, 0.0f), bounds.maxY + std::max(sweep.y, 0.0f),
                           bounds.maxZ + std::max(sweep.z, 0.0f));

      if (!reachesGround(*ground, low, high, contactMargin + ground->volume->voxelSize()))
      {
        return std::nullopt;
      }

      return ground;
    }
  }

  bool isPolytopeCollider(const Collider& collider)
  {
    return collider.getColliderType() == ColliderType::boxCollider ||
      collider.getColliderType() == ColliderType::convexMeshCollider;
  }

  // Built on every call from the collider's own world caches: generating the points is cheap next to sampling them,
  // and a cache on the collider would be written by several threads in the parallel narrow phase.
  std::vector<glm::vec3> polytopeQueryPoints(Collider& collider, const TerrainGround& ground,
                                             terrain::Sampler& sampler, const glm::vec3& offset)
  {
    const auto geometry = geometryOf(collider);

    return cappedPoints(geometry, groupsOf(geometry, ground, sampler, offset), ground.volume->voxelSize());
  }

  std::vector<Contact> findPolytopeTerrainContacts(Collider& collider, Terrain& terrain, const glm::vec3& sweep)
  {
    const auto ground = groundWithin(collider, terrain, sweep);
    if (!ground)
    {
      return {};
    }

    terrain::Sampler sampler(*ground->volume);

    // The collider is taken to where its bounding sphere first nears the ground; the contacts are then moved back to
    // where it is.
    float fraction = 0.0f;
    const auto [center, radius] = boundingSphereOf(collider);
    if (const auto reached = reachAlongSweep(sampler, ground->frame, center, radius, sweep,
                                             ground->volume->voxelSize()))
    {
      fraction = *reached;
    }

    const auto offset = sweep * fraction;
    const auto remaining = sweep - offset;

    std::vector<TerrainCandidate> reaching;
    for (const auto& candidate : measuredPoints(collider, *ground, sampler, remaining, offset))
    {
      if (reachesAlong(candidate, remaining))
      {
        reaching.push_back(candidate);
      }
    }

    std::vector<Contact> contacts;
    for (auto& cluster : clusterByWall(std::move(reaching)))
    {
      if (fraction > 0.0f)
      {
        trimToNearest(cluster, ground->volume->voxelSize());
      }

      contacts.push_back(makeTerrainContact(cluster, polytopeManifoldPoints, remaining));
      shiftTracedContact(contacts.back(), sweep, fraction);
    }

    return contacts;
  }

  bool polytopeIntersectsTerrain(Collider& collider, Terrain& terrain)
  {
    const auto ground = groundWithin(collider, terrain, glm::vec3(0));
    if (!ground)
    {
      return false;
    }

    terrain::Sampler sampler(*ground->volume);

    return std::ranges::any_of(measuredPoints(collider, *ground, sampler, glm::vec3(0), glm::vec3(0)),
                               [](const TerrainCandidate& candidate)
    {
      return candidate.distance < 0.0f;
    });
  }
}
