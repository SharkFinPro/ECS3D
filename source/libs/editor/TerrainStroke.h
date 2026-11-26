#ifndef TERRAINSTROKE_H
#define TERRAINSTROKE_H

#include <terrain/TerrainBrush.h>
#include <terrain/TerrainChunk.h>
#include <terrain/TerrainOps.h>
#include <terrain/TerrainRaycast.h>
#include <terrain/TerrainTypes.h>
#include <terrain/TerrainVolume.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

// One press-drag-release of a brush.
class TerrainStroke
{
public:
  // Bounds one moveTo, so a huge pointer jump cannot flood the network with applications.
  static constexpr size_t maxApplicationsPerMove = 256;

  static constexpr float minSpacing = 1.0e-4f;

  struct Finished
  {
    uint64_t strokeId = 0;
    // Every chunk the stroke changed, with its pointer from before the stroke's first change to it (null = was air).
    std::vector<std::pair<terrain::ChunkCoord, std::shared_ptr<const terrain::TerrainChunk>>> before;
    // The union of the dirty boxes of every recorded application.
    terrain::SampleBox box;
  };

  [[nodiscard]] bool active() const
  {
    return m_active;
  }

  [[nodiscard]] uint64_t strokeId() const
  {
    return m_strokeId;
  }

  [[nodiscard]] const terrain::SampleBox& box() const
  {
    return m_box;
  }

  // Starts a stroke at the first surface hit and returns the first application. templateOp supplies kind, radius,
  // strength, falloff and material; the returned op's center is hit.point. For flatten, the plane is captured here
  // and kept for the whole stroke. A non-finite hit point is refused: the template comes back unchanged and active()
  // stays false (a stroke already in progress is left alone). Beginning while a stroke is active discards its
  // record: the caller ends the old stroke first.
  terrain::BrushOp begin(const uint64_t strokeId, const terrain::BrushOp& templateOp, const terrain::RayHit& hit)
  {
    if (!finite(hit.point))
    {
      return templateOp;
    }

    m_active = true;
    m_strokeId = strokeId;
    m_template = templateOp;
    m_last = glm::dvec3(hit.point);
    m_sinceLast = 0.0;
    m_before.clear();
    m_seen.clear();
    m_box = {};

    if (m_template.kind == terrain::BrushOp::Kind::flatten)
    {
      m_template.planePoint = hit.point;
      m_template.planeNormal = planeNormalFor(hit.normal);
    }

    return opAt(hit.point);
  }

  // The pointer is over the surface at hit. Returns the applications due since the last one: points spaced
  // spacing() apart along the piecewise-linear path through successive hit points, carrying the leftover distance
  // from one call to the next. Empty when not active or the pointer did not move far enough. At most
  // maxApplicationsPerMove are returned; past that only the last ones of the segment are kept, so the brush ends at
  // hit rather than lagging behind it.
  [[nodiscard]] std::vector<terrain::BrushOp> moveTo(const terrain::RayHit& hit)
  {
    std::vector<terrain::BrushOp> due;

    if (!m_active || !finite(hit.point))
    {
      return due;
    }

    const glm::dvec3 target(hit.point);
    const glm::dvec3 delta = target - m_last;
    const double length = glm::length(delta);

    if (!std::isfinite(length) || length <= 0.0)
    {
      return due;
    }

    const glm::dvec3 direction = delta / length;
    const double step = static_cast<double>(spacing());
    const double slack = step * 1.0e-9;
    const double total = std::floor((m_sinceLast + length + slack) / step);
    const double count = std::min(total, static_cast<double>(maxApplicationsPerMove));

    for (double k = total - count + 1.0; k <= total; k += 1.0)
    {
      const double travelled = std::min(k * step - m_sinceLast, length);
      due.push_back(opAt(glm::vec3(m_last + direction * travelled)));
    }

    m_sinceLast = std::max(0.0, m_sinceLast + length - total * step);
    m_last = target;
    return due;
  }

  // A quarter of the template radius, never below a tiny positive floor so a zero radius cannot loop forever.
  [[nodiscard]] float spacing() const
  {
    const float quarter = m_template.radius * 0.25f;
    return std::isfinite(quarter) ? std::max(quarter, minSpacing) : minSpacing;
  }

  // Call for each application BEFORE committing its edits to the volume: captures the pre-stroke pointer of every
  // coordinate in edits seen for the first time in this stroke, and grows box() by dirtyBox(volume, op). Recording
  // after the commit makes the before list hold post-edit pointers, so an undo restores nothing.
  void recordApplication(const terrain::TerrainVolume& volume, const terrain::BrushOp& op,
                         const terrain::ChunkEdits& edits)
  {
    if (!m_active)
    {
      return;
    }

    for (const auto& edit : edits)
    {
      if (m_seen.insert(edit.first).second)
      {
        m_before.emplace_back(edit.first, volume.chunk(edit.first));
      }
    }

    const terrain::SampleBox dirty = terrain::dirtyBox(volume, op);

    if (dirty.empty())
    {
      return;
    }

    if (m_box.empty())
    {
      m_box = dirty;
    }
    else
    {
      m_box.min = glm::min(m_box.min, dirty.min);
      m_box.max = glm::max(m_box.max, dirty.max);
    }
  }

  // Ends the stroke. nullopt when none was active. A stroke that recorded nothing still returns its id, with an
  // empty before list and an empty box.
  std::optional<Finished> end()
  {
    if (!m_active)
    {
      return std::nullopt;
    }

    Finished finished;
    finished.strokeId = m_strokeId;
    finished.before = std::move(m_before);
    finished.box = m_box;

    m_active = false;
    m_before.clear();
    m_seen.clear();
    m_box = {};
    return finished;
  }

private:
  [[nodiscard]] static bool finite(const glm::vec3& value)
  {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
  }

  [[nodiscard]] static glm::vec3 planeNormalFor(const glm::vec3& normal)
  {
    if (finite(normal))
    {
      const float length = glm::length(normal);

      if (std::isfinite(length) && length > 1.0e-6f)
      {
        return normal / length;
      }
    }

    return { 0.0f, 1.0f, 0.0f };
  }

  [[nodiscard]] terrain::BrushOp opAt(const glm::vec3& center) const
  {
    terrain::BrushOp op = m_template;
    op.center = center;
    return op;
  }

  bool m_active = false;
  uint64_t m_strokeId = 0;
  terrain::BrushOp m_template;
  glm::dvec3 m_last{ 0.0 };
  double m_sinceLast = 0.0;
  std::vector<std::pair<terrain::ChunkCoord, std::shared_ptr<const terrain::TerrainChunk>>> m_before;
  std::unordered_set<terrain::ChunkCoord> m_seen;
  terrain::SampleBox m_box;
};

// Each side of a redistance box is at most this many samples, so the grown grid stays within maxRedistanceSamples.
inline constexpr int maxRedistanceSide = 158;

// Splits a stroke box into boxes terrain::redistance accepts, covering exactly the input box with no overlap.
[[nodiscard]] inline std::vector<terrain::SampleBox> redistanceBoxes(const terrain::SampleBox& box)
{
  std::vector<terrain::SampleBox> pieces;

  if (box.empty())
  {
    return pieces;
  }

  const auto splitAxis = [&box](const int axis)
  {
    std::vector<std::pair<int, int>> ranges;

    for (int64_t low = box.min[axis]; low < box.max[axis]; low += maxRedistanceSide)
    {
      ranges.emplace_back(static_cast<int>(low),
                          static_cast<int>(std::min<int64_t>(low + maxRedistanceSide, box.max[axis])));
    }

    return ranges;
  };

  const auto xs = splitAxis(0);
  const auto ys = splitAxis(1);
  const auto zs = splitAxis(2);

  for (const auto& z : zs)
  {
    for (const auto& y : ys)
    {
      for (const auto& x : xs)
      {
        pieces.push_back({ { x.first, y.first, z.first }, { x.second, y.second, z.second } });
      }
    }
  }

  return pieces;
}

// As above, but only the pieces whose chunks include one of the touched chunks (a stroke's Finished::before
// coordinates), so a long diagonal stroke costs work in proportion to what it touched rather than to its bounding
// box. A piece that covers any touched chunk is kept whole. Each piece's redistance reads a one-sample margin from
// its neighbors, so the caller commits each piece's edits before computing the next.
[[nodiscard]] inline std::vector<terrain::SampleBox> redistanceBoxes(const terrain::SampleBox& box,
                                                                     const std::span<const terrain::ChunkCoord> touched)
{
  std::vector<terrain::SampleBox> pieces;

  if (box.empty())
  {
    return pieces;
  }

  std::set<std::tuple<int64_t, int64_t, int64_t>> cells;

  for (const terrain::ChunkCoord coord : touched)
  {
    const glm::ivec3 origin = terrain::chunkOrigin(coord);
    int64_t low[3];
    int64_t high[3];
    bool overlaps = true;

    for (int axis = 0; axis < 3; ++axis)
    {
      low[axis] = std::max<int64_t>(origin[axis], box.min[axis]);
      high[axis] = std::min<int64_t>(static_cast<int64_t>(origin[axis]) + terrain::chunkSize, box.max[axis]);
      overlaps = overlaps && low[axis] < high[axis];
    }

    if (!overlaps)
    {
      continue;
    }

    for (int64_t z = (low[2] - box.min.z) / maxRedistanceSide; z <= (high[2] - 1 - box.min.z) / maxRedistanceSide; ++z)
    {
      for (int64_t y = (low[1] - box.min.y) / maxRedistanceSide; y <= (high[1] - 1 - box.min.y) / maxRedistanceSide;
           ++y)
      {
        for (int64_t x = (low[0] - box.min.x) / maxRedistanceSide;
             x <= (high[0] - 1 - box.min.x) / maxRedistanceSide; ++x)
        {
          cells.emplace(x, y, z);
        }
      }
    }
  }

  for (const auto& [x, y, z] : cells)
  {
    const int64_t cell[3] = { x, y, z };
    terrain::SampleBox piece;

    for (int axis = 0; axis < 3; ++axis)
    {
      const int64_t low = box.min[axis] + cell[axis] * maxRedistanceSide;
      piece.min[axis] = static_cast<int>(low);
      piece.max[axis] = static_cast<int>(std::min<int64_t>(low + maxRedistanceSide, box.max[axis]));
    }

    pieces.push_back(piece);
  }

  return pieces;
}

#endif //TERRAINSTROKE_H
