#ifndef TERRAINBRUSHSESSION_H
#define TERRAINBRUSHSESSION_H

#include "TerrainStroke.h"
#include "TerrainToolState.h"
#include <EditorTerrainSettings.h>
#include <terrain/TerrainBrush.h>
#include <terrain/TerrainEdit.h>
#include <terrain/TerrainOps.h>
#include <terrain/TerrainRaycast.h>
#include <terrain/TerrainReplication.h>
#include <terrain/TerrainVolume.h>
#include <Protocol.h>
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>
#include <utility>
#include <uuid.h>

// What the pointer is doing over the viewport in one frame.
struct TerrainPointer
{
  bool pressed = false;
  // Whether the button was already down last frame, so a drag that starts on a panel and enters the scene begins
  // no stroke.
  bool wasPressed = false;
  bool sceneHovered = false;
  // The camera fly (right drag) owns the pointer.
  bool flying = false;
  bool hasHit = false;
};

// Looking through a scene camera (viewingSceneCamera) leaves the pointer to the game, as it does the gizmo.
[[nodiscard]] constexpr TerrainPointer makeTerrainPointer(const bool pressed, const bool wasPressed,
                                                          const bool sceneHovered, const bool viewingSceneCamera,
                                                          const bool flying, const bool hasHit)
{
  return { pressed, wasPressed, sceneHovered && !viewingSceneCamera, flying, hasHit };
}

enum class TerrainStrokeStep : uint8_t { none, begin, move, end };

[[nodiscard]] constexpr TerrainStrokeStep nextStrokeStep(const bool strokeActive, const TerrainPointer& pointer)
{
  if (strokeActive)
  {
    if (!pointer.pressed)
    {
      return TerrainStrokeStep::end;
    }

    // The camera fly owns the pointer, so a held stroke neither paints nor ends while it moves the view.
    return pointer.sceneHovered && !pointer.flying && pointer.hasHit ? TerrainStrokeStep::move
                                                                      : TerrainStrokeStep::none;
  }

  const bool fresh = pointer.pressed && !pointer.wasPressed;
  return fresh && pointer.sceneHovered && !pointer.flying && pointer.hasHit ? TerrainStrokeStep::begin
                                                                            : TerrainStrokeStep::none;
}

// The radius a brush works at: the server refuses one above maxBrushRadiusVoxels voxels, so strokes and the cursor ring
// both use the clamped value.
[[nodiscard]] inline float effectiveBrushRadius(const float radius, const terrain::TerrainVolume& volume)
{
  return std::min(radius, terrain::maxBrushRadiusVoxels * volume.voxelSize());
}

// The operation a stroke of tool applies at each point; material is what Paint writes and what the ground Add and
// Flatten raise takes.
[[nodiscard]] inline terrain::BrushOp terrainBrushTemplate(const TerrainTool tool,
                                                           const editorTerrainSettings::BrushSettings& brush,
                                                           const uint8_t material)
{
  terrain::BrushOp op;
  op.kind = brushKind(tool);
  op.material = material;
  op.radius = brush.radius;
  // Paint sets materials at full weight, and a stored strength of 0 would make the brush skip every sample.
  op.strength = op.kind == terrain::BrushOp::Kind::paint ? 1.0f : brush.strength;
  op.falloff = op.kind == terrain::BrushOp::Kind::paint ? 0.0f : brush.falloff;
  return op;
}

// Performs a stroke's applications against the editor's own volume and sends each one as a terrainEdit, so the view
// predicts what the server will compute. The store's revisions are never touched. The view holds the stroke's touched
// chunks out of the server's echo (TerrainViewSync::apply), since the echo describes a server that is behind the
// prediction. At release the session asks the server for the final state of every touched chunk, and the hold lasts
// until that answer arrives: an echo flushed between the server's last brush and its redistance still describes the
// unfinished stroke. The answer arrives after the redistance and replaces the prediction with the confirmed chunks and
// their revisions. An answer that never comes (a project load, a replaced volume, a failed flush) must not hold the
// terrain forever, so the wait also ends when the store's volume is no longer the one the stroke finished on, on a
// new load and after awaitTimeout.
class TerrainBrushSession
{
public:
  using Send = std::function<void(const net::Message&)>;

  // Long enough for the server's next echo flush and the round trip, short enough that a lost answer is not noticed.
  static constexpr std::chrono::seconds awaitTimeout{ 5 };

  // A pointer held still applies the brush again this often. A stroke without a snapshot surface (Subtract) aims at
  // the live terrain, so each application already reaches deeper; it repeats at surfaceRefresh instead, to dig at the
  // pace a held Add grows.
  static constexpr std::chrono::milliseconds holdInterval{ 100 };

  // While the pointer is held still, the surface the cursor aims at catches up with the stroke this often, so a held
  // Add grows a mound toward the camera and a held Subtract digs deeper, at a pace far below one step per frame.
  static constexpr std::chrono::milliseconds surfaceRefresh{ 250 };

  [[nodiscard]] bool active() const
  {
    return m_stroke.active();
  }

  [[nodiscard]] const std::shared_ptr<terrain::TerrainVolume>& volume() const
  {
    return m_volume;
  }

  // The terrain as it was when the active stroke began, for the cursor to aim at (strokeCursorHit), so a stroke
  // follows the ground it started on instead of climbing the material it adds; null when no stroke is active and for
  // a Subtract stroke, which aims at the live terrain.
  [[nodiscard]] const std::shared_ptr<const terrain::TerrainVolume>& strokeSurface() const
  {
    return m_surface;
  }

  // Where the active stroke's last application landed.
  [[nodiscard]] glm::vec3 strokePoint() const
  {
    return m_stroke.lastPoint();
  }

  // How far one frame's cursor may jump along the surface before the stroke keeps to its upright plane instead
  // (strokeCursorHit's maxJump): two brush radii.
  [[nodiscard]] float strokeJumpLimit() const
  {
    return 2.0f * m_stroke.radius();
  }

  [[nodiscard]] const uuids::uuid& terrain() const
  {
    return m_terrain;
  }

  // The chunks the active stroke has changed so far, to hold out of the echo; empty when no stroke is active.
  [[nodiscard]] const std::unordered_set<terrain::ChunkCoord>& touched() const
  {
    return m_stroke.touched();
  }

  // The hold for an echo message about the given terrain: the touched chunks while a stroke of that terrain is
  // active, plus the chunks of a released stroke whose final state has not arrived; none when there are neither.
  [[nodiscard]] const terrain::ChunkHold* holdFor(const uuids::uuid& terrainUuid)
  {
    if (terrainUuid != m_terrain || (!m_stroke.active() && m_awaiting.empty()))
    {
      return nullptr;
    }

    if (!m_stroke.active())
    {
      return &m_awaiting;
    }

    m_hold = m_awaiting;
    m_hold.insert(m_stroke.touched().begin(), m_stroke.touched().end());
    return &m_hold;
  }

  // Call with each terrainChunks message before applying it: a chunk the server sent in full ends the wait for that
  // chunk, so the message about to be applied replaces the prediction. Hash-only entries (an echo of an edit the view
  // predicted) leave the wait alone.
  void confirmChunks(const terrain::TerrainChunksPayload& payload)
  {
    if (payload.terrain != m_terrain || m_awaiting.empty())
    {
      return;
    }

    for (const terrain::ReceivedChunk& entry : payload.entries)
    {
      if (entry.chunk)
      {
        m_awaiting.erase(entry.coord);
      }
    }

    if (m_awaiting.empty())
    {
      m_awaitingVolume.reset();
    }
  }

  // Drops the wait when the store's volume for the released stroke's terrain is not the one the stroke finished on:
  // the answer describes a volume that is gone.
  void dropAwaitingIfReplaced(const std::shared_ptr<terrain::TerrainVolume>& current)
  {
    if (!m_awaiting.empty() && current != m_awaitingVolume)
    {
      clearAwaiting();
    }
  }

  // Drops the wait once awaitTimeout has passed since the release, as a backstop for an answer that never arrives.
  void expireAwaiting(const std::chrono::steady_clock::time_point now)
  {
    if (!m_awaiting.empty() && now - m_awaitingSince >= awaitTimeout)
    {
      clearAwaiting();
    }
  }

  void clearAwaiting()
  {
    m_awaiting.clear();
    m_awaitingVolume.reset();
  }

  [[nodiscard]] bool awaitingFinalChunks() const
  {
    return !m_awaiting.empty();
  }

  // Starts a stroke and performs its first application. False, with nothing sent, when a stroke is already active,
  // the volume is null or the hit is not finite. templateOp's radius is clamped to what the server accepts.
  bool begin(const uuids::uuid& terrainUuid, const std::shared_ptr<terrain::TerrainVolume>& volume,
             terrain::BrushOp templateOp, const terrain::RayHit& hit, const Send& send)
  {
    if (m_stroke.active() || !volume)
    {
      return false;
    }

    templateOp.radius = effectiveBrushRadius(templateOp.radius, *volume);

    const terrain::BrushOp first = m_stroke.begin(m_nextStrokeId, templateOp, hit);

    if (!m_stroke.active())
    {
      return false;
    }

    if (terrainUuid != m_terrain)
    {
      clearAwaiting();
    }

    m_terrain = terrainUuid;
    m_volume = volume;
    m_redistances = strokeRedistances(templateOp.kind);
    // Subtract only ever takes material away, so it cannot climb toward the camera; it aims at the live terrain, so
    // erasing reaches whatever is still there, nearer than the press or not.
    m_surface = templateOp.kind == terrain::BrushOp::Kind::subtract ? nullptr : snapshotSurface(*volume);
    ++m_nextStrokeId;
    perform(first, send);
    return true;
  }

  // Performs every application the pointer's move since the last call made due. Returns how many ran.
  size_t moveTo(const terrain::RayHit& hit, const Send& send)
  {
    size_t performed = 0;

    for (const terrain::BrushOp& op : m_stroke.moveTo(hit))
    {
      if (perform(op, send))
      {
        ++performed;
      }
    }

    return performed;
  }

  // Ends the stroke and sends one redistance for each piece of its box that holds a chunk the stroke touched, after
  // applying the same redistance to the volume, piece by piece in the order the server will, so each piece sees the
  // one before it. Then asks for the server's final state of every touched chunk. Returns the finished stroke, or
  // nullopt when none was active. Its before list holds every chunk the stroke or its redistance changed, which is
  // what an undo needs, and predicted is true. A stroke that changed nothing sends nothing.
  // The stroke is over before the first message is sent, so if send throws the exception propagates, the session is
  // idle and the messages not yet sent are lost.
  std::optional<TerrainStroke::Finished> end(const Send& send)
  {
    return finish(true, send);
  }

  // Ends the stroke for a frame that knows the store's current volume for the terrain and whether the connection can
  // still edit. Without editing the stroke is dropped and nothing is sent. When the volume was replaced under the
  // stroke the server still applied its brushes, so the redistance pieces (which depend only on the stroke's box and
  // touched chunks) are sent, but the replaced volume is left alone and no final state is requested, and the returned
  // stroke's predicted is false.
  std::optional<TerrainStroke::Finished> endFor(const std::shared_ptr<terrain::TerrainVolume>& current,
                                                const bool editable, const Send& send)
  {
    if (!m_stroke.active())
    {
      return std::nullopt;
    }

    if (!editable)
    {
      cancel();
      return std::nullopt;
    }

    return finish(current == m_volume, send);
  }

  struct Advance
  {
    // A brush application ran, so the volume and the terrain's bounds moved.
    bool changed = false;
    // The stroke ended this frame (it was active before and is not now).
    bool ended = false;
    // The stroke that ended this frame with its prediction committed to the volume and something recorded, for the
    // caller to record as an undo step; unset for a stroke whose volume was replaced, one dropped for lack of editing
    // and one that changed nothing.
    std::optional<TerrainStroke::Finished> finished;
  };

  // One frame of the pointer. makeTemplate is called only when a stroke begins, so the brush settings are read once
  // per press. A frame that cannot paint (no hit, off the scene, the camera flying) breaks the stroke's path so the
  // next hit does not bridge the gap. A volume that differs from the stroke's ends it as endFor does. Without editing
  // no stroke begins: nothing is predicted and nothing is sent. With now, a stroke held still applies again every
  // holdInterval and refreshes its surface every surfaceRefresh; without it a still pointer applies nothing.
  Advance advance(const uuids::uuid& terrainUuid, const std::shared_ptr<terrain::TerrainVolume>& volume,
                  const bool editable, TerrainPointer pointer, const std::optional<terrain::RayHit>& hit,
                  const std::function<terrain::BrushOp()>& makeTemplate, const Send& send,
                  const std::optional<std::chrono::steady_clock::time_point> now = std::nullopt)
  {
    Advance result;
    const bool wasActive = m_stroke.active();
    pointer.hasHit = hit.has_value();

    if (!wasActive && !editable)
    {
      return result;
    }

    if (wasActive && volume != m_volume)
    {
      static_cast<void>(endFor(volume, editable, send));
      result.ended = !m_stroke.active();
      return result;
    }

    switch (nextStrokeStep(wasActive, pointer))
    {
      case TerrainStrokeStep::begin:
        result.changed = begin(terrainUuid, volume, makeTemplate(), *hit, send);

        if (now)
        {
          m_lastApplication = *now;
          m_surfaceTakenAt = *now;
        }
        break;
      case TerrainStrokeStep::move:
        // A Subtract stroke aims at the live terrain, so the cursor leaving the hole it dug lands on the surface well
        // above the hole's bottom. Bridging that rise would cut a straight trench through the ground at the hole's
        // depth, so a height change of more than a brush radius starts a new segment at the hit instead. A fast drag
        // across level ground still bridges.
        if (!m_surface && std::abs(hit->point.y - m_stroke.lastPoint().y) > m_stroke.radius())
        {
          m_stroke.breakPath();
        }

        result.changed = moveTo(*hit, send) > 0;

        if (now)
        {
          result.changed = hold(*hit, *now, result.changed, send) || result.changed;
        }
        break;
      case TerrainStrokeStep::end:
        if (std::optional<TerrainStroke::Finished> finished = endFor(volume, editable, send);
            finished && finished->predicted && !finished->before.empty())
        {
          result.finished = std::move(finished);
        }
        break;
      case TerrainStrokeStep::none:
        if (wasActive && pointer.pressed)
        {
          m_stroke.breakPath();
        }
        break;
    }

    result.ended = wasActive && !m_stroke.active();
    return result;
  }

  // Drops the stroke and the wait for a released one without sending anything, for a volume that was replaced under
  // it or a connection that is gone.
  void cancel()
  {
    static_cast<void>(m_stroke.end());
    m_volume.reset();
    m_surface.reset();
    clearAwaiting();
  }

private:
  std::optional<TerrainStroke::Finished> finish(const bool commitLocally, const Send& send)
  {
    std::optional<TerrainStroke::Finished> finished = m_stroke.end();
    m_surface.reset();

    if (!finished)
    {
      return finished;
    }

    const std::shared_ptr<terrain::TerrainVolume> volume = std::exchange(m_volume, nullptr);

    std::vector<terrain::ChunkCoord> touched;
    touched.reserve(finished->before.size());

    for (const auto& entry : finished->before)
    {
      touched.push_back(entry.first);
    }

    std::vector<terrain::ChunkCoord> requested = touched;
    std::unordered_set<terrain::ChunkCoord> known(touched.begin(), touched.end());

    const std::vector<terrain::SampleBox> pieces =
      m_redistances ? redistanceBoxes(finished->box, touched) : std::vector<terrain::SampleBox>{};

    for (const terrain::SampleBox& piece : pieces)
    {
      if (commitLocally)
      {
        const terrain::ChunkEdits edits = terrain::redistance(*volume, piece);

        // A chunk only the redistance changed joins before with its pointer from ahead of this commit, so an undo
        // restores it too.
        for (const auto& edit : edits)
        {
          if (known.insert(edit.first).second)
          {
            requested.push_back(edit.first);
            finished->before.emplace_back(edit.first, volume->chunk(edit.first));
          }
        }

        terrain::commit(*volume, edits);
      }

      send(terrain::packRedistanceEdit(m_terrain, finished->strokeId, piece));
    }

    // A send that throws leaves nothing awaited: no answer is coming for a request that was not sent in full.
    if (commitLocally)
    {
      finished->predicted = true;
      requestFinalChunks(requested, send);
      m_awaiting.insert(requested.begin(), requested.end());
      m_awaitingVolume = volume;
      m_awaitingSince = std::chrono::steady_clock::now();
    }

    return finished;
  }

  // Chunk requests ride the same ordered connection as the edits, so the server answers after the redistance.
  void requestFinalChunks(const std::vector<terrain::ChunkCoord>& coords, const Send& send) const
  {
    for (size_t first = 0; first < coords.size(); first += terrain::maxTerrainChunksEntries)
    {
      const size_t last = std::min(coords.size(), first + terrain::maxTerrainChunksEntries);
      std::vector<terrain::ChunkEntry> entries;

      for (size_t index = first; index < last; ++index)
      {
        entries.push_back(terrain::makeHashOnlyEntry(coords[index], 1, 0));
      }

      send(terrain::packTerrainChunks(m_terrain, entries));
    }
  }

  // A frame of the stroke: an application from movement restarts the hold clock; a still pointer applies again once
  // holdInterval has passed, and its surface catches up once surfaceRefresh has.
  bool hold(const terrain::RayHit& hit, const std::chrono::steady_clock::time_point now, const bool moved,
            const Send& send)
  {
    if (moved)
    {
      m_lastApplication = now;
      return false;
    }

    bool applied = false;

    if (now - m_lastApplication >= (m_surface ? holdInterval : std::chrono::milliseconds(surfaceRefresh)))
    {
      m_lastApplication = now;

      if (const std::optional<terrain::BrushOp> op = m_stroke.holdAt(hit))
      {
        applied = perform(*op, send);
      }
    }

    if (m_surface && now - m_surfaceTakenAt >= surfaceRefresh)
    {
      m_surfaceTakenAt = now;
      m_surface = snapshotSurface(*m_volume);
    }

    return applied;
  }

  // A brush the server would refuse (a center out of range) is neither applied nor sent.
  bool perform(const terrain::BrushOp& op, const Send& send)
  {
    if (terrain::dirtyBox(*m_volume, op).empty())
    {
      return false;
    }

    const terrain::ChunkEdits edits = terrain::applyBrush(*m_volume, op);
    m_stroke.recordApplication(*m_volume, op, edits);
    terrain::commit(*m_volume, edits);
    send(terrain::packBrushEdit(m_terrain, m_stroke.strokeId(), op));
    return true;
  }

  // Copies the chunk pointers only: chunks are immutable, so later edits never reach the copy.
  [[nodiscard]] static std::shared_ptr<const terrain::TerrainVolume> snapshotSurface(const terrain::TerrainVolume& volume)
  {
    auto surface = std::make_shared<terrain::TerrainVolume>(volume.voxelSize());
    surface->restoreChunks(volume.snapshotChunks());
    return surface;
  }

  TerrainStroke m_stroke;
  std::shared_ptr<const terrain::TerrainVolume> m_surface;
  std::chrono::steady_clock::time_point m_lastApplication;
  std::chrono::steady_clock::time_point m_surfaceTakenAt;
  uuids::uuid m_terrain;
  std::shared_ptr<terrain::TerrainVolume> m_volume;
  // Chunks of a released stroke of m_terrain that were requested and have not arrived in full.
  terrain::ChunkHold m_awaiting;
  std::shared_ptr<terrain::TerrainVolume> m_awaitingVolume;
  std::chrono::steady_clock::time_point m_awaitingSince;
  terrain::ChunkHold m_hold;
  bool m_redistances = true;
  uint64_t m_nextStrokeId = 1;
};

#endif //TERRAINBRUSHSESSION_H
