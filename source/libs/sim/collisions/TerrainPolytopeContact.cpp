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
#include "terrain/TerrainTypes.h"
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

    // In voxels: keeps the all-air test on the safe side of measuredPoints' own reject distance, whose sweep length is
    // what is left after the trace and so no longer, up to rounding.
    constexpr float airSlack = 1e-3f;

    // Covers the rounding in reachesAlong's own comparison, so furthestReach errs toward measuring a point.
    constexpr float reachSlack = 1e-4f;

    // The furthest raw distance a query point can read and still give a contact along sweep. Its candidate is never
    // nearer the ground than that reading, and reachesAlong accepts none past the margin and the part of the sweep
    // closing on the ground, which is no more than the sweep's length.
    [[nodiscard]] float furthestReach(const glm::vec3& sweep)
    {
      return contactMargin + glm::length(sweep) * (1.0f + reachSlack) + reachSlack;
    }

    using Edge = std::pair<glm::vec3, glm::vec3>;

    // A box's own, or a hull's shared from the collider's caches.
    struct Geometry {
      std::vector<glm::vec3> ownedVertices;
      std::vector<Edge> ownedEdges;
      std::vector<WorldFace> ownedFaces;
      const std::vector<glm::vec3>* sharedVertices = nullptr;
      const std::vector<Edge>* sharedEdges = nullptr;
      const std::vector<WorldFace>* sharedFaces = nullptr;

      [[nodiscard]] std::span<const glm::vec3> vertices() const
      {
        return sharedVertices != nullptr ? std::span<const glm::vec3>(*sharedVertices)
                                         : std::span<const glm::vec3>(ownedVertices);
      }

      [[nodiscard]] std::span<const Edge> edges() const
      {
        return sharedEdges != nullptr ? std::span<const Edge>(*sharedEdges) : std::span<const Edge>(ownedEdges);
      }

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
      geometry.ownedVertices = boxCorners(box);

      for (int mask = 0; mask < 8; ++mask)
      {
        for (int axis = 0; axis < 3; ++axis)
        {
          if ((mask & (1 << axis)) == 0)
          {
            geometry.ownedEdges.emplace_back(geometry.ownedVertices[static_cast<size_t>(mask)],
                                             geometry.ownedVertices[static_cast<size_t>(mask | (1 << axis))]);
          }
        }
      }

      geometry.ownedFaces = supportFacesOf(box);

      return geometry;
    }

    [[nodiscard]] Geometry hullGeometry(ConvexMeshCollider& hull)
    {
      Geometry geometry;
      geometry.sharedVertices = &hull.getWorldVertices();
      geometry.sharedFaces = &hullSupportFaces(hull);
      geometry.sharedEdges = &hullSupportEdges(hull);

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

    void appendFacePoints(const WorldFace& face, const FaceGrid& grid, std::vector<glm::vec3>& points)
    {
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

      appendFacePoints(*group.face, gridOf(*group.face, spacing), points);
    }

    [[nodiscard]] std::vector<Group> groupsOf(const Geometry& geometry)
    {
      std::vector<Group> groups;
      groups.reserve(geometry.edges().size() + geometry.faces().size());

      for (const auto& edge : geometry.edges())
      {
        groups.push_back({ &edge, nullptr, 0.0f });
      }

      for (const auto& face : geometry.faces())
      {
        if (face.corners.size() >= 3)
        {
          groups.push_back({ nullptr, &face, 0.0f });
        }
      }

      return groups;
    }

    // Only needed to share out a budget the groups overrun, so groups are ranked only then.
    void rankGroups(std::vector<Group>& groups, const TerrainGround& ground, terrain::Sampler& sampler,
                    const glm::vec3& offset)
    {
      for (auto& group : groups)
      {
        const auto middle = group.edge != nullptr ? 0.5f * (group.edge->first + group.edge->second)
                                                  : group.face->center;
        group.rank = std::round(sampler.distance(ground.frame.toLocal(middle + offset)) * rankQuantum);
      }
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

    [[nodiscard]] std::vector<glm::vec3> cappedPoints(const Geometry& geometry, const TerrainGround& ground,
                                                      terrain::Sampler& sampler, const glm::vec3& offset)
    {
      const float voxelSize = ground.volume->voxelSize();
      auto groups = groupsOf(geometry);

      const auto vertices = geometry.vertices();
      const size_t budget = vertices.size() < maxTerrainQueryPoints ? maxTerrainQueryPoints - vertices.size() : 0;

      // Each face's grid at the finest spacing, built once for both counting its points and making them.
      std::vector<FaceGrid> finestGrids(groups.size());
      size_t full = 0;
      for (size_t i = 0; i < groups.size(); ++i)
      {
        if (groups[i].face != nullptr)
        {
          finestGrids[i] = gridOf(*groups[i].face, voxelSize);
          full += static_cast<size_t>(finestGrids[i].cellsU) * static_cast<size_t>(finestGrids[i].cellsV);
        }
        else
        {
          full += pointBound(groups[i], voxelSize);
        }
      }

      std::vector<float> spacings(groups.size(), voxelSize);
      if (full > budget)
      {
        rankGroups(groups, ground, sampler, offset);

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

      std::vector<glm::vec3> points;
      points.reserve(vertices.size() + std::min(full, budget));
      points.insert(points.end(), vertices.begin(), vertices.end());
      for (size_t i = 0; i < groups.size(); ++i)
      {
        if (groups[i].face != nullptr && spacings[i] == voxelSize)
        {
          appendFacePoints(*groups[i].face, finestGrids[i], points);
        }
        else if (spacings[i] > 0.0f)
        {
          appendPoints(groups[i], spacings[i], points);
        }
      }

      return points;
    }

    struct Measured {
      std::vector<TerrainCandidate> candidates;
      // What the same points measured with no sweep would say: whether any is inside the ground.
      bool touching = false;
    };

    // Measures each query point, taken offset along from where it is, against the ground that is near enough to touch
    // with what is left of the sweep (furthestReach): a point further off could give no contact, so it is skipped
    // before its surface is looked for. One deeper than the band is stepped out of the ground against the sweep.
    // With judgeTouching, which needs a zero offset, the points are also judged as measuring them with no sweep
    // would.
    [[nodiscard]] Measured measuredPoints(Collider& collider, const TerrainGround& ground, terrain::Sampler& sampler,
                                          const glm::vec3& sweep, const glm::vec3& offset, const bool judgeTouching)
    {
      const float voxelSize = ground.volume->voxelSize();
      const float reject = furthestReach(sweep);
      const auto backward = stepBackDirection(ground.frame, sweep);
      const auto touchBackward = stepBackDirection(ground.frame, glm::vec3(0));

      const auto points = polytopeQueryPoints(collider, ground, sampler, offset);

      Measured measured;
      measured.candidates.reserve(points.size());
      for (const auto& placed : points)
      {
        const auto world = placed + offset;
        const auto local = ground.frame.toLocal(world);
        const float raw = sampler.distance(local) * voxelSize;
        if (raw > reject)
        {
          continue;
        }

        float centerDistance = raw;
        auto surface = findSurface(sampler, local);
        const bool flat = !surface;
        if (flat)
        {
          surface = findDeepSurface(sampler, local, backward, voxelSize, raw);
          centerDistance = unknownDepth;
        }

        std::optional<TerrainCandidate> candidate;
        if (surface)
        {
          candidate = measureCandidate(ground.frame, *surface, local, centerDistance, 0.0f);
          candidate->point = world;
          measured.candidates.push_back(*candidate);
        }

        // A candidate is no nearer the ground than its point's reading, so only a point inside it can touch.
        if (!judgeTouching || measured.touching || !(raw < 0.0f))
        {
          continue;
        }

        // Only a point stepped out of the ground depends on the direction it steps along.
        if (flat && backward != touchBackward)
        {
          candidate.reset();
          if (const auto touchSurface = findDeepSurface(sampler, local, touchBackward, voxelSize, raw))
          {
            candidate = measureCandidate(ground.frame, *touchSurface, local, unknownDepth, 0.0f);
          }
        }

        measured.touching = candidate && candidate->distance < 0.0f;
      }

      return measured;
    }

    // How far along sweep the collider first comes near the ground: its bounding sphere (the center of its bounds,
    // out to its furthest vertex) is traced, or, when that is too big to see the ground through the band, each vertex
    // as a point and the earliest taken. Nothing is done for a sweep of a voxel or less.
    [[nodiscard]] float traceFraction(Collider& collider, const TerrainGround& ground, terrain::Sampler& sampler,
                                      const glm::vec3& sweep)
    {
      const float voxelSize = ground.volume->voxelSize();
      if (!(glm::length(sweep) > voxelSize))
      {
        return 0.0f;
      }

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

      if (radius < 0.5f * terrain::bandVoxels * voxelSize)
      {
        return reachAlongSweep(sampler, ground.frame, center, radius, sweep, voxelSize).value_or(0.0f);
      }

      std::optional<float> earliest;
      for (const auto& vertex : vertices)
      {
        if (const auto reached = reachAlongSweep(sampler, ground.frame, vertex, 0.0f, sweep, voxelSize))
        {
          earliest = std::min(earliest.value_or(1.0f), *reached);
        }
      }

      return earliest.value_or(0.0f);
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

      const float voxelSize = ground->volume->voxelSize();
      if (!reachesGround(*ground, low, high, contactMargin + voxelSize))
      {
        return std::nullopt;
      }

      // Each query point lies in the swept bounds and reads the cell around it, so when every sample there is air each
      // reads the band's edge, and measuredPoints skips them all while that is further than the furthest it accepts.
      const float airReading = terrain::decodeDistance(terrain::airDistance) * voxelSize;
      if (airReading > furthestReach(sweep) + airSlack * voxelSize)
      {
        const auto samples = reachableSamples(*ground, low, high, voxelSize);
        if (samples && ground->volume->isAirWithin(*samples))
        {
          return std::nullopt;
        }
      }

      return ground;
    }

    struct PolytopeProbe {
      std::vector<Contact> contacts;
      // Left empty when judging it would take a measure of its own.
      std::optional<bool> touching;
    };

    // The collider is taken to where it first nears the ground; the contacts are then moved back to where it is.
    // With judgeTouching, whether it is inside the ground now is judged from the same measure when the sweep is not
    // traced, since the query points are then the ones it stands at.
    [[nodiscard]] PolytopeProbe probePolytope(Collider& collider, Terrain& terrain, const glm::vec3& sweep,
                                              const bool judgeTouching)
    {
      PolytopeProbe probe;

      // The unswept bounds lie inside the swept ones and are tested against a nearer reach, so they find no ground
      // either.
      const auto ground = groundWithin(collider, terrain, sweep);
      if (!ground)
      {
        if (judgeTouching)
        {
          probe.touching = false;
        }

        return probe;
      }

      terrain::Sampler sampler(*ground->volume);

      const bool longSweep = glm::length(sweep) > ground->volume->voxelSize();
      const float fraction = traceFraction(collider, *ground, sampler, sweep);

      const auto offset = sweep * fraction;
      const auto remaining = sweep - offset;

      bool judgeHere = false;
      if (judgeTouching && fraction == 0.0f)
      {
        judgeHere = sweep == glm::vec3(0) || groundWithin(collider, terrain, glm::vec3(0)).has_value();
        probe.touching = false;
      }

      auto measured = measuredPoints(collider, *ground, sampler, remaining, offset, judgeHere);
      if (judgeHere)
      {
        probe.touching = measured.touching;
      }

      std::vector<TerrainCandidate> reaching;
      for (const auto& candidate : measured.candidates)
      {
        if (reachesAlong(candidate, remaining))
        {
          reaching.push_back(candidate);
        }
      }

      for (auto& cluster : clusterByWall(std::move(reaching)))
      {
        if (longSweep)
        {
          trimToNearest(cluster, 0.5f * ground->volume->voxelSize());
        }

        probe.contacts.push_back(makeTerrainContact(cluster, polytopeManifoldPoints, remaining));
        shiftTracedContact(probe.contacts.back(), sweep, fraction);
      }

      return probe;
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
    return cappedPoints(geometryOf(collider), ground, sampler, offset);
  }

  std::vector<Contact> findPolytopeTerrainContacts(Collider& collider, Terrain& terrain, const glm::vec3& sweep)
  {
    return probePolytope(collider, terrain, sweep, false).contacts;
  }

  bool polytopeIntersectsTerrain(Collider& collider, Terrain& terrain)
  {
    const auto ground = groundWithin(collider, terrain, glm::vec3(0));
    if (!ground)
    {
      return false;
    }

    terrain::Sampler sampler(*ground->volume);

    return std::ranges::any_of(measuredPoints(collider, *ground, sampler, glm::vec3(0), glm::vec3(0), false).candidates,
                               [](const TerrainCandidate& candidate)
    {
      return candidate.distance < 0.0f;
    });
  }

  TerrainProbe probePolytopeTerrain(Collider& collider, Terrain& terrain, const glm::vec3& sweep)
  {
    auto probe = probePolytope(collider, terrain, sweep, true);
    const bool touching = probe.touching ? *probe.touching : polytopeIntersectsTerrain(collider, terrain);

    return { touching, std::move(probe.contacts) };
  }
}
