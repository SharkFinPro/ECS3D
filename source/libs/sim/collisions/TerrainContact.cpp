#include "TerrainContact.h"
#include "FaceManifold.h"
#include "ManifoldInternal.h"
#include "TerrainContactInternal.h"
#include "objects/Object.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/CapsuleCollider.h"
#include "objects/components/collisions/Collider.h"
#include "objects/components/collisions/SphereCollider.h"
#include "objects/components/collisions/Terrain.h"
#include "terrain/TerrainSampling.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace collisions {
  namespace {
    // cos(15 degrees): normals closer than this count as one wall.
    constexpr float sameWallCosine = 0.9659258f;

    // The smallest depth a contact is reported with, so a point hovering within the margin still has a direction.
    constexpr float minimumContactDepth = 1e-6f;

    // Depths and ordering are compared at this resolution so float noise cannot pick which of equally deep points
    // along a capsule leads.
    constexpr float depthQuantum = 1e3f;

    constexpr float maxSampleCoordinate = 1e7f;

    // A trilinear cell holding a crease reads a gradient shorter than one voxel per voxel, and its normal blends the
    // two walls.
    constexpr float reliableGradientLength = 0.9f;

    struct RoundShape {
      glm::vec3 start;
      glm::vec3 end;
      float radius;
      bool sphere;
    };

    using detail::findSurface;
    using detail::reachesAlong;
    using Surface = detail::TerrainSurface;
    using detail::TerrainCandidate;

    struct Setup : detail::TerrainGround {
      RoundShape shape;
    };

    struct CorePoint {
      Surface surface;
      glm::vec3 center;
      float centerDistance;
      TerrainCandidate candidate;
    };

    struct TerrainPoint {
      glm::vec3 point;
      float depth;
    };

    using Cluster = std::vector<TerrainCandidate>;

    [[nodiscard]] std::optional<RoundShape> roundShapeOf(Collider& collider)
    {
      if (collider.getColliderType() == ColliderType::capsuleCollider)
      {
        auto& capsule = dynamic_cast<CapsuleCollider&>(collider);
        const auto [start, end] = capsule.getSegment();

        return RoundShape{ start, end, capsule.getRadius(), false };
      }

      if (collider.getColliderType() == ColliderType::sphereCollider)
      {
        const auto center = collider.getPosition();

        return RoundShape{ center, center, dynamic_cast<SphereCollider&>(collider).getRadius(), true };
      }

      return std::nullopt;
    }

    [[nodiscard]] bool chunkHoldsGround(const terrain::TerrainVolume& volume, const terrain::ChunkCoord coord)
    {
      const auto chunk = volume.chunk(coord);

      return chunk && !chunk->isAir();
    }

    // The samples of the terrain-local box that the world-space box from low to high can reach.
    [[nodiscard]] std::optional<terrain::SampleBox> reachableSamples(const detail::TerrainGround& ground,
                                                                     const glm::vec3& low, const glm::vec3& high,
                                                                     const float reach)
    {
      const float voxelSize = ground.volume->voxelSize();

      glm::vec3 localLow(std::numeric_limits<float>::max());
      glm::vec3 localHigh(std::numeric_limits<float>::lowest());
      for (int corner = 0; corner < 8; ++corner)
      {
        const glm::vec3 world((corner & 1) != 0 ? high.x + reach : low.x - reach,
                              (corner & 2) != 0 ? high.y + reach : low.y - reach,
                              (corner & 4) != 0 ? high.z + reach : low.z - reach);
        const auto local = ground.frame.toLocal(world);
        localLow = glm::min(localLow, local);
        localHigh = glm::max(localHigh, local);
      }

      if (!std::isfinite(glm::dot(localLow, localLow) + glm::dot(localHigh, localHigh)))
      {
        return std::nullopt;
      }

      const auto toSamples = [voxelSize](const glm::vec3& local)
      {
        return glm::clamp(local / voxelSize, -maxSampleCoordinate, maxSampleCoordinate);
      };

      return terrain::SampleBox{ glm::ivec3(glm::floor(toSamples(localLow))),
                                 glm::ivec3(glm::ceil(toSamples(localHigh))) + 1 };
    }
  }

  namespace detail {
    // Walks the chunks of the box, or the volume's own chunks when there are fewer, so a huge box costs no more than
    // the terrain holds.
    bool reachesGround(const TerrainGround& ground, const glm::vec3& low, const glm::vec3& high, const float reach)
    {
      const auto box = reachableSamples(ground, low, high, reach);
      if (!box || box->empty())
      {
        return false;
      }

      const auto first = terrain::chunkOfSample(box->min);
      const auto last = terrain::chunkOfSample(box->max - glm::ivec3(1));
      const auto& volume = *ground.volume;

      const int64_t inBox = static_cast<int64_t>(last.x - first.x + 1) * static_cast<int64_t>(last.y - first.y + 1) *
        static_cast<int64_t>(last.z - first.z + 1);

      if (inBox > static_cast<int64_t>(volume.chunks().size()))
      {
        return std::ranges::any_of(volume.chunks(), [&first, &last](const auto& entry)
        {
          const auto& coord = entry.first;

          return coord.x >= first.x && coord.x <= last.x && coord.y >= first.y && coord.y <= last.y &&
            coord.z >= first.z && coord.z <= last.z && entry.second && !entry.second->isAir();
        });
      }

      for (int32_t z = first.z; z <= last.z; ++z)
      {
        for (int32_t y = first.y; y <= last.y; ++y)
        {
          for (int32_t x = first.x; x <= last.x; ++x)
          {
            if (chunkHoldsGround(volume, { x, y, z }))
            {
              return true;
            }
          }
        }
      }

      return false;
    }

    std::optional<TerrainGround> prepareGround(Terrain& terrain)
    {
      if (!terrain.getCollisionEnabled() || terrain.getOwner() == nullptr)
      {
        return std::nullopt;
      }

      auto volume = terrain.volume();
      const auto transform = terrain.getOwner()->getComponent<Transform>(ComponentType::transform);
      if (!volume || !transform || !(volume->voxelSize() > 0.0f))
      {
        return std::nullopt;
      }

      const auto rotation = transform->getOrientation();

      return TerrainGround{ std::move(volume), { transform->getPosition(), rotation, glm::inverse(rotation) } };
    }
  }

  namespace {
    [[nodiscard]] std::optional<Setup> prepare(Collider& collider, Terrain& terrain, const glm::vec3& sweep)
    {
      const auto shape = roundShapeOf(collider);
      if (!shape)
      {
        return std::nullopt;
      }

      auto ground = detail::prepareGround(terrain);
      if (!ground)
      {
        return std::nullopt;
      }

      const auto low = glm::min(glm::min(shape->start, shape->end),
                                glm::min(shape->start + sweep, shape->end + sweep));
      const auto high = glm::max(glm::max(shape->start, shape->end),
                                 glm::max(shape->start + sweep, shape->end + sweep));
      const float reach = shape->radius + contactMargin + ground->volume->voxelSize();

      if (!detail::reachesGround(*ground, low, high, reach))
      {
        return std::nullopt;
      }

      return Setup{ std::move(*ground), *shape };
    }
  }

  namespace detail {
    // The ground surface nearest from, refined once: the first estimate runs along the gradient at from, the second
    // along the gradient at that estimate.
    std::optional<TerrainSurface> findSurface(terrain::Sampler& sampler, const glm::vec3& from)
    {
      const auto gradient = sampler.gradient(from);
      const float length = glm::length(gradient);
      if (!(length >= terrain::minGradientLength))
      {
        return std::nullopt;
      }

      const auto firstNormal = gradient / length;
      auto point = from - firstNormal * sampler.metricDistance(from);

      const auto refined = sampler.gradient(point);
      const float refinedLength = glm::length(refined);
      const auto normal = refinedLength >= terrain::minGradientLength ? refined / refinedLength : firstNormal;

      point -= normal * sampler.metricDistance(point);

      return TerrainSurface{ point, normal, length };
    }

    // The tangent plane at the surface point is the exact distance on a concave wall but passes under the shape on a
    // ridge or dome, so the field's own distance at the center bounds it from below. The raw field is used rather than
    // metricDistance, which overshoots in a crease cell.
    TerrainCandidate measureCandidate(const TerrainFrame& frame, const TerrainSurface& surface,
                                      const glm::vec3& center, const float centerDistance, const float radius)
    {
      const float planeDistance = glm::dot(center - surface.point, surface.normal) - radius;

      return { frame.pointToWorld(surface.point), frame.rotation * surface.normal,
               std::max(planeDistance, centerDistance - radius) };
    }

    bool reachesAlong(const TerrainCandidate& candidate, const glm::vec3& sweep)
    {
      return candidate.distance < contactMargin + std::max(glm::dot(sweep, -candidate.normal), 0.0f);
    }
  }

  namespace {
    [[nodiscard]] TerrainCandidate measure(const Setup& setup, const Surface& surface, const glm::vec3& center,
                                           const float centerDistance)
    {
      return detail::measureCandidate(setup.frame, surface, center, centerDistance, setup.shape.radius);
    }

    [[nodiscard]] std::vector<glm::vec3> queryPoints(const RoundShape& shape, const float voxelSize)
    {
      const float length = glm::distance(shape.start, shape.end);
      if (!(length > 0.0f))
      {
        return { shape.start };
      }

      const int segments = std::max(1, static_cast<int>(std::ceil(length / voxelSize)));
      std::vector<glm::vec3> points;
      points.reserve(static_cast<size_t>(segments) + 1);

      for (int i = 0; i <= segments; ++i)
      {
        points.push_back(glm::mix(shape.start, shape.end, static_cast<float>(i) / static_cast<float>(segments)));
      }

      return points;
    }

    [[nodiscard]] std::vector<CorePoint> probeCore(const Setup& setup, terrain::Sampler& sampler)
    {
      std::vector<CorePoint> cores;

      for (const auto& world : queryPoints(setup.shape, setup.volume->voxelSize()))
      {
        const auto center = setup.frame.toLocal(world);
        if (const auto surface = findSurface(sampler, center))
        {
          const float centerDistance = sampler.distance(center) * setup.volume->voxelSize();
          cores.push_back({ *surface, center, centerDistance, measure(setup, *surface, center, centerDistance) });
        }
      }

      return cores;
    }

    [[nodiscard]] bool sameWall(const glm::vec3& first, const glm::vec3& second)
    {
      return glm::dot(first, second) >= sameWallCosine;
    }

    [[nodiscard]] std::array<glm::vec3, 4> probeOffsets(const glm::vec3& normal, const float radius)
    {
      const auto reference = std::abs(normal.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
      const auto first = glm::normalize(glm::cross(normal, reference)) * (0.5f * radius);
      const auto second = glm::normalize(glm::cross(normal, first)) * (0.5f * radius);

      return { first, -first, second, -second };
    }

    // A single sample at the center blends the two walls of a V gully into one normal, so a sphere also looks from
    // beside its center. A probe on the same wall as one already kept adds nothing, and on a convex hilltop its
    // tangent plane would overstate the depth, so only a probe that sees another wall is kept.
    [[nodiscard]] std::vector<TerrainCandidate> wallProbes(const Setup& setup, terrain::Sampler& sampler,
                                                           const CorePoint& core, const glm::vec3& sweep)
    {
      std::vector<TerrainCandidate> kept;

      for (const auto& offset : probeOffsets(core.surface.normal, setup.shape.radius))
      {
        const auto surface = findSurface(sampler, core.center + offset);
        if (!surface)
        {
          continue;
        }

        const auto candidate = measure(setup, *surface, core.center, core.centerDistance);
        const bool seen = sameWall(candidate.normal, core.candidate.normal) ||
          std::ranges::any_of(kept, [&candidate](const TerrainCandidate& other)
          {
            return sameWall(candidate.normal, other.normal);
          });

        if (!seen && reachesAlong(candidate, sweep))
        {
          kept.push_back(candidate);
        }
      }

      return kept;
    }

    // Whether two walls face each other, each in front of the other's plane.
    [[nodiscard]] bool formsGully(const std::vector<TerrainCandidate>& walls, const float slack)
    {
      return std::ranges::any_of(walls, [&walls, slack](const TerrainCandidate& wall)
      {
        return std::ranges::any_of(walls, [&wall, slack](const TerrainCandidate& other)
        {
          return glm::dot(other.point - wall.point, wall.normal) > slack;
        });
      });
    }

    [[nodiscard]] std::vector<TerrainCandidate> gatherCandidates(const Setup& setup, terrain::Sampler& sampler,
                                                                 const glm::vec3& sweep)
    {
      const auto cores = probeCore(setup, sampler);

      std::vector<TerrainCandidate> candidates;
      for (const auto& core : cores)
      {
        if (reachesAlong(core.candidate, sweep))
        {
          candidates.push_back(core.candidate);
        }
      }

      if (setup.shape.sphere && !cores.empty())
      {
        const auto probes = wallProbes(setup, sampler, cores.front(), sweep);

        // The core of a sphere over a gully apex between samples is a normal blended from both walls and is deeper
        // than either, so the walls the probes found replace it.
        const float voxelSize = setup.volume->voxelSize();
        if (cores.front().surface.gradientLength * voxelSize < reliableGradientLength &&
            formsGully(probes, 0.1f * voxelSize))
        {
          candidates.clear();
        }

        candidates.insert(candidates.end(), probes.begin(), probes.end());
      }

      return candidates;
    }

    [[nodiscard]] float quantized(const float distance)
    {
      return std::round(distance * depthQuantum);
    }
  }

  namespace detail {
    std::vector<TerrainCluster> clusterByWall(std::vector<TerrainCandidate> candidates)
    {
      std::ranges::stable_sort(candidates, [](const TerrainCandidate& a, const TerrainCandidate& b)
      {
        return quantized(a.distance) < quantized(b.distance);
      });

      std::vector<TerrainCluster> clusters;
      for (const auto& candidate : candidates)
      {
        const auto home = std::ranges::find_if(clusters, [&candidate](const TerrainCluster& cluster)
        {
          return sameWall(cluster.front().normal, candidate.normal);
        });

        if (home != clusters.end())
        {
          home->push_back(candidate);
        }
        else if (clusters.size() < maxTerrainManifolds)
        {
          clusters.push_back({ candidate });
        }
      }

      return clusters;
    }
  }

  namespace {
    // At most maxPoints: the deepest, then each time the one furthest from those chosen, so a capsule lying on the
    // ground rests on both ends, an upright one on a single point and a box on its corners.
    [[nodiscard]] std::vector<TerrainPoint> spreadPoints(const Cluster& cluster, const size_t maxPoints)
    {
      std::vector<detail::Candidate> candidates;
      candidates.reserve(cluster.size());
      for (const auto& member : cluster)
      {
        candidates.push_back({ member.point, std::round(-member.distance * depthQuantum) / depthQuantum });
      }

      std::vector<TerrainPoint> points;
      for (const auto& chosen : detail::spreadOut(candidates, maxPoints))
      {
        const bool repeated = std::ranges::any_of(points, [&chosen](const TerrainPoint& other)
        {
          return glm::distance(other.point, chosen) < detail::dedupeDistance;
        });
        const auto member = std::ranges::find_if(cluster, [&chosen](const TerrainCandidate& other)
        {
          return other.point == chosen;
        });

        if (!repeated && member != cluster.end())
        {
          points.push_back({ chosen, -member->distance });
        }
      }

      return points;
    }

  }

  namespace detail {
    Contact makeTerrainContact(const TerrainCluster& cluster, const size_t maxPoints, const glm::vec3& sweep)
    {
      const auto& seed = cluster.front();
      const auto points = maxPoints > 1 ? spreadPoints(cluster, maxPoints)
                                        : std::vector<TerrainPoint>{ { seed.point, -seed.distance } };

      float overlap = points.front().depth;
      for (const auto& point : points)
      {
        overlap = std::max(overlap, point.depth);
      }

      float advance = std::max(glm::dot(sweep, -seed.normal), 0.0f);
      if (!(overlap + advance > minimumContactDepth))
      {
        advance = minimumContactDepth - overlap;
      }

      Contact contact{ seed.normal * (overlap + advance), points.front().point };
      contact.advance = advance;

      if (points.size() > 1)
      {
        contact.pointCount = static_cast<std::uint8_t>(std::min(points.size(), contact.points.size()));
        for (size_t i = 0; i < contact.pointCount; ++i)
        {
          contact.points[i] = points[i].point;
          contact.depths[i] = points[i].depth;
        }
      }

      return contact;
    }
  }

  std::vector<Contact> findTerrainContacts(Collider& collider, Terrain& terrain, const glm::vec3& sweep)
  {
    if (detail::isPolytopeCollider(collider))
    {
      return detail::findPolytopeTerrainContacts(collider, terrain, sweep);
    }

    const auto setup = prepare(collider, terrain, sweep);
    if (!setup)
    {
      return {};
    }

    terrain::Sampler sampler(*setup->volume);

    std::vector<Contact> contacts;
    for (const auto& cluster : detail::clusterByWall(gatherCandidates(*setup, sampler, sweep)))
    {
      contacts.push_back(detail::makeTerrainContact(cluster, setup->shape.sphere ? 1 : 2, sweep));
    }

    return contacts;
  }

  bool intersectsTerrain(Collider& collider, Terrain& terrain)
  {
    if (detail::isPolytopeCollider(collider))
    {
      return detail::polytopeIntersectsTerrain(collider, terrain);
    }

    const auto setup = prepare(collider, terrain, glm::vec3(0));
    if (!setup)
    {
      return false;
    }

    terrain::Sampler sampler(*setup->volume);

    return std::ranges::any_of(probeCore(*setup, sampler), [](const CorePoint& core)
    {
      return core.candidate.distance < 0.0f;
    });
  }

  bool approachesTerrain(Collider& collider, Terrain& terrain, const glm::vec3& sweep)
  {
    return !findTerrainContacts(collider, terrain, sweep).empty();
  }
}
