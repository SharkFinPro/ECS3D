#include "Gizmo.h"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace {
  // The screen-space length every handle is sized to, whatever the camera distance - the "constant
  // screen size" gizmos are expected to have.
  constexpr float handleLengthPixels = 100.0f;
  constexpr float hoverPixels = 8.0f;
  constexpr float shaftPixels = 3.0f;
  constexpr float tipLengthPixels = 16.0f;
  constexpr float tipHalfWidthPixels = 6.0f;
  constexpr float uniformHandleHalfPixels = 6.0f;
  constexpr int ringSegments = 64;
  constexpr float minScaleFactor = 0.01f;

  // Two handles within this of each other in screen distance are treated as a tie, broken by which one is
  // more reliable to have meant (see handleReliability).
  constexpr float hitTestTieEpsilonPixels = 0.5f;

  // Below this, the mouse ray and the drag axis/plane are treated as degenerate and the previous frame's
  // result is kept instead of dividing by (near) zero.
  constexpr float parallelEpsilon = 1e-4f;
  constexpr float clipEpsilon = 1e-6f;

  // About 10 degrees off the rotate plane - past this the ray-plane intersection gets too imprecise and
  // the screen-space fallback takes over.
  constexpr float edgeOnDotThreshold = 0.17f;

  [[nodiscard]] bool isFinite(const glm::vec3& v)
  {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
  }

  [[nodiscard]] bool isFinite(const gizmo::Pose& pose)
  {
    return isFinite(pose.position) && isFinite(pose.rotation) && isFinite(pose.scale);
  }

  [[nodiscard]] bool isIdentity(const gizmo::ParentFrame& parent)
  {
    return parent.position == glm::vec3(0.0f) && parent.orientation == glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
      && parent.scale == glm::vec3(1.0f);
  }

  // Inverse of world = parentPos + parentQ * (parentScale * local). An axis whose parent scale is zero or
  // whose result is not finite keeps its previous local value.
  [[nodiscard]] glm::vec3 worldToLocalPosition(const gizmo::ParentFrame& parent, const glm::vec3& worldPosition,
                                               const glm::vec3& previousLocal)
  {
    const glm::vec3 unrotated = glm::inverse(parent.orientation) * (worldPosition - parent.position);
    glm::vec3 local = previousLocal;

    for (int i = 0; i < 3; ++i)
    {
      if (parent.scale[i] == 0.0f)
      {
        continue;
      }

      const float value = unrotated[i] / parent.scale[i];
      if (std::isfinite(value))
      {
        local[i] = value;
      }
    }

    return local;
  }

  [[nodiscard]] bool viewportIsUsable(const gizmo::Rect& viewport)
  {
    return viewport.width >= 1.0f && viewport.height >= 1.0f;
  }

  [[nodiscard]] glm::mat4 projectionMatrix(const gizmo::View& view)
  {
    return glm::perspective(glm::radians(view.fovDegrees), view.viewport.width / view.viewport.height,
                            view.nearPlane, view.farPlane);
  }

  [[nodiscard]] glm::vec3 cameraPosition(const gizmo::View& view)
  {
    return glm::vec3(glm::inverse(view.view)[3]);
  }

  // World length of a handle sized to read as handleLengthPixels on screen at the gizmo's own distance
  // from the camera.
  [[nodiscard]] float worldHandleLength(const gizmo::View& view, const glm::vec3& center)
  {
    const float distance = glm::length(center - cameraPosition(view));
    const float worldPerPixel =
      2.0f * distance * std::tan(glm::radians(view.fovDegrees * 0.5f)) / view.viewport.height;

    return handleLengthPixels * worldPerPixel;
  }

  [[nodiscard]] float snapValue(const float value, const float step)
  {
    if (!std::isfinite(step) || step <= 0.0f)
    {
      return value;
    }

    return std::round(value / step) * step;
  }

  [[nodiscard]] int axisIndex(const gizmo::Handle handle)
  {
    switch (handle)
    {
      case gizmo::Handle::y:
        return 1;
      case gizmo::Handle::z:
        return 2;
      default:
        return 0;
    }
  }

  // Scale always uses the object's own axes (scale is applied in local space); translate/rotate follow
  // Space.
  [[nodiscard]] glm::vec3 axisForHandle(const gizmo::Mode mode, const gizmo::Space space,
                                        const gizmo::Handle handle, const glm::vec3& worldRotationDegrees)
  {
    const int index = axisIndex(handle);

    if (mode == gizmo::Mode::scale || space == gizmo::Space::local)
    {
      const glm::mat3 axes = glm::mat3_cast(glm::quat(glm::radians(worldRotationDegrees)));
      return glm::normalize(axes[index]);
    }

    glm::vec3 axis(0.0f);
    axis[index] = 1.0f;
    return axis;
  }

  [[nodiscard]] float distancePointToSegment(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b)
  {
    const glm::vec2 ab = b - a;
    const float lenSq = glm::dot(ab, ab);
    float t = lenSq > 1e-9f ? glm::dot(p - a, ab) / lenSq : 0.0f;
    t = glm::clamp(t, 0.0f, 1.0f);

    return glm::length(p - (a + ab * t));
  }

  // Standard closest point between the drag axis line and the mouse ray, solved for the axis line's own
  // parameter s. nullopt when the two are nearly parallel (denom near zero) - the caller keeps the
  // previous frame's amount in that case.
  [[nodiscard]] std::optional<float> closestLineParam(const glm::vec3& lineOrigin, const glm::vec3& lineDir,
                                                       const gizmo::Ray& ray)
  {
    const glm::vec3 w0 = lineOrigin - ray.origin;
    const float b = glm::dot(lineDir, ray.direction);
    const float d = glm::dot(lineDir, w0);
    const float e = glm::dot(ray.direction, w0);
    const float denom = 1.0f - b * b;

    if (denom < parallelEpsilon)
    {
      return std::nullopt;
    }

    return (b * e - d) / denom;
  }

  [[nodiscard]] std::optional<glm::vec3> rayPlaneHit(const gizmo::Ray& ray, const glm::vec3& center,
                                                      const glm::vec3& normal)
  {
    const float denom = glm::dot(ray.direction, normal);

    if (std::abs(denom) < 1e-6f)
    {
      return std::nullopt;
    }

    const float t = glm::dot(center - ray.origin, normal) / denom;

    if (t < 0.0f)
    {
      return std::nullopt;
    }

    return ray.origin + ray.direction * t;
  }

  // Any unit vector perpendicular to axis - used where a fallback "current grab direction" is needed but
  // there is no real hit to derive one from. A fixed world axis would be degenerate (or nearly so) when
  // axis itself is close to that world axis, e.g. rotating about X - picking whichever of X/Y is farther
  // from axis avoids that.
  [[nodiscard]] glm::vec3 anyPerpendicular(const glm::vec3& axis)
  {
    const glm::vec3 helper = std::abs(axis.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::normalize(glm::cross(axis, helper));
  }

  [[nodiscard]] std::vector<glm::vec3> buildRingPoints(const glm::vec3& center, const glm::vec3& normal,
                                                        const float radius)
  {
    const glm::vec3 upHint = std::abs(normal.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 u = glm::normalize(glm::cross(upHint, normal));
    const glm::vec3 v = glm::cross(normal, u);

    std::vector<glm::vec3> points;
    points.reserve(ringSegments);

    for (int i = 0; i < ringSegments; ++i)
    {
      const float angle = 2.0f * glm::pi<float>() * static_cast<float>(i) / static_cast<float>(ringSegments);
      points.push_back(center + radius * (std::cos(angle) * u + std::sin(angle) * v));
    }

    return points;
  }

  // World-space geometry for one handle, shared by hit-testing and primitive emission so drawing and
  // hit-testing can never drift apart.
  struct HandleGeom {
    gizmo::Handle handle = gizmo::Handle::none;
    glm::vec3 center{ 0.0f };
    glm::vec3 tip{ 0.0f };   // translate/scale shaft end; unused for rotate
    glm::vec3 axis{ 0.0f };  // direction (translate/scale) or plane normal (rotate)
    std::vector<glm::vec3> ringPoints; // rotate only
  };

  [[nodiscard]] std::vector<HandleGeom> buildHandleGeoms(const gizmo::Mode mode, const gizmo::Space space,
                                                          const glm::vec3& center,
                                                          const glm::vec3& worldRotationDegrees,
                                                          const gizmo::View& view)
  {
    std::vector<HandleGeom> result;
    const float worldLen = worldHandleLength(view, center);

    if (mode == gizmo::Mode::scale)
    {
      HandleGeom uniform;
      uniform.handle = gizmo::Handle::uniform;
      uniform.center = center;
      uniform.tip = center;
      result.push_back(uniform);
    }

    for (const gizmo::Handle handle : { gizmo::Handle::x, gizmo::Handle::y, gizmo::Handle::z })
    {
      HandleGeom g;
      g.handle = handle;
      g.center = center;
      g.axis = axisForHandle(mode, space, handle, worldRotationDegrees);

      if (mode == gizmo::Mode::rotate)
      {
        g.ringPoints = buildRingPoints(center, g.axis, worldLen);
      }
      else
      {
        g.tip = center + g.axis * worldLen;
      }

      result.push_back(g);
    }

    return result;
  }

  // The screen-space distance from mouse to g's hover geometry (a ring's 64-segment polyline for rotate,
  // otherwise the center-to-tip shaft), or nullopt when none of it projects (behind the camera).
  [[nodiscard]] std::optional<float> nearestDistanceForHandle(const gizmo::Mode mode, const HandleGeom& g,
                                                               const gizmo::View& view, const glm::vec2& mouse)
  {
    if (mode == gizmo::Mode::rotate)
    {
      std::optional<float> best;

      for (std::size_t i = 0; i < g.ringPoints.size(); ++i)
      {
        const auto pa = gizmo::project(view, g.ringPoints[i]);
        const auto pb = gizmo::project(view, g.ringPoints[(i + 1) % g.ringPoints.size()]);

        if (!pa || !pb)
        {
          continue;
        }

        const float d = distancePointToSegment(mouse, *pa, *pb);
        if (!best || d < *best)
        {
          best = d;
        }
      }

      return best;
    }

    const auto pa = gizmo::project(view, g.center);
    const auto pb = gizmo::project(view, g.tip);

    if (!pa || !pb)
    {
      return std::nullopt;
    }

    return distancePointToSegment(mouse, *pa, *pb);
  }

  [[nodiscard]] glm::vec3 viewForward(const gizmo::View& view)
  {
    return glm::normalize(glm::vec3(glm::inverse(view.view) * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
  }

  // How reliable a handle is to have actually meant, for breaking a screen-distance tie between two of
  // them - higher wins. A rotate ring is most reliable (most circular, easiest to read) when its plane
  // faces the camera, i.e. its normal is aligned with the view direction; an edge-on ring, normal
  // perpendicular to view, degenerates to a line (the scenario this tie-break exists for). A translate/
  // scale shaft is the opposite: it is most reliable (longest, clearest on screen) when its axis is
  // perpendicular to view, and least when the axis points straight down the view (foreshortened to a
  // point).
  [[nodiscard]] float handleReliability(const gizmo::Mode mode, const glm::vec3& axis, const glm::vec3& forward)
  {
    const float alignment = std::abs(glm::dot(axis, forward));
    return mode == gizmo::Mode::rotate ? alignment : -alignment;
  }

  [[nodiscard]] gizmo::Handle hitTest(const gizmo::Mode mode, const std::vector<HandleGeom>& handles,
                                      const glm::vec3& center, const gizmo::View& view, const glm::vec2& mouse)
  {
    if (mode == gizmo::Mode::scale)
    {
      if (const auto centerScreen = gizmo::project(view, center))
      {
        if (std::abs(mouse.x - centerScreen->x) <= uniformHandleHalfPixels &&
            std::abs(mouse.y - centerScreen->y) <= uniformHandleHalfPixels)
        {
          return gizmo::Handle::uniform;
        }
      }
    }

    struct Candidate {
      gizmo::Handle handle;
      float dist;
      float reliability; // see handleReliability
    };

    const glm::vec3 forward = viewForward(view);
    std::vector<Candidate> candidates;

    for (const auto& g : handles)
    {
      if (g.handle == gizmo::Handle::uniform)
      {
        continue;
      }

      const auto dist = nearestDistanceForHandle(mode, g, view, mouse);
      if (!dist || *dist >= hoverPixels)
      {
        continue;
      }

      candidates.push_back({ g.handle, *dist, handleReliability(mode, g.axis, forward) });
    }

    if (candidates.empty())
    {
      return gizmo::Handle::none;
    }

    // Two handles can genuinely tie in screen distance - e.g. a rotate ring viewed edge-on projects to a
    // line through the center, which a face-on ring's own circle can also pass through. Rather than let
    // iteration order settle that arbitrarily, the closest handle wins outright, and among handles within
    // hitTestTieEpsilonPixels of it, the more reliable one wins (see handleReliability).
    float minDist = candidates.front().dist;
    for (const auto& c : candidates)
    {
      minDist = std::min(minDist, c.dist);
    }

    gizmo::Handle best = gizmo::Handle::none;
    float bestReliability = -std::numeric_limits<float>::infinity();

    for (const auto& c : candidates)
    {
      if (c.dist <= minDist + hitTestTieEpsilonPixels && c.reliability > bestReliability)
      {
        bestReliability = c.reliability;
        best = c.handle;
      }
    }

    return best;
  }

  void appendQuad(std::vector<gizmo::Triangle>& triangles, const glm::vec3& p0, const glm::vec3& p1,
                  const glm::vec3& p2, const glm::vec3& p3, const gizmo::Handle handle,
                  const gizmo::Highlight highlight)
  {
    triangles.push_back(gizmo::Triangle{ p0, p1, p2, handle, highlight });
    triangles.push_back(gizmo::Triangle{ p0, p2, p3, handle, highlight });
  }

  // A camera-facing ribbon along [a, b]. Skipped (no quad) when the axis points straight at or away from
  // the camera, since the side vector degenerates there.
  void appendShaftQuad(std::vector<gizmo::Triangle>& triangles, const glm::vec3& a, const glm::vec3& b,
                       const float halfWidth, const glm::vec3& cameraPos, const gizmo::Handle handle,
                       const gizmo::Highlight highlight)
  {
    const glm::vec3 axisDir = b - a;
    const float axisLen = glm::length(axisDir);

    if (axisLen < 1e-6f)
    {
      return;
    }

    const glm::vec3 axisUnit = axisDir / axisLen;
    const glm::vec3 toCamera = cameraPos - a;
    glm::vec3 side = glm::cross(axisUnit, toCamera);
    const float sideLen = glm::length(side);

    if (sideLen < 1e-6f)
    {
      return;
    }

    side = side / sideLen * halfWidth;
    appendQuad(triangles, a - side, a + side, b + side, b - side, handle, highlight);
  }

  // A flat camera-facing arrowhead whose apex sits exactly at the tip, so its farthest vertex still lands
  // on the hover segment.
  void appendArrowTip(std::vector<gizmo::Triangle>& triangles, const glm::vec3& tip, const glm::vec3& axisUnit,
                      const float worldPerPixel, const glm::vec3& cameraPos, const gizmo::Handle handle,
                      const gizmo::Highlight highlight)
  {
    const glm::vec3 toCamera = cameraPos - tip;
    glm::vec3 side = glm::cross(axisUnit, toCamera);
    const float sideLen = glm::length(side);

    if (sideLen < 1e-6f)
    {
      return;
    }

    side = side / sideLen * (tipHalfWidthPixels * worldPerPixel);
    const glm::vec3 base = tip - axisUnit * (tipLengthPixels * worldPerPixel);

    triangles.push_back(gizmo::Triangle{ base - side, base + side, tip, handle, highlight });
  }

  // A small camera-facing square, used for the scale caps and the uniform (center) handle.
  void appendSquare(std::vector<gizmo::Triangle>& triangles, const glm::vec3& center, const float halfSize,
                    const glm::vec3& cameraPos, const gizmo::Handle handle, const gizmo::Highlight highlight)
  {
    const glm::vec3 toCameraDir = glm::normalize(cameraPos - center);

    if (!isFinite(toCameraDir))
    {
      return;
    }

    const glm::vec3 upHint =
      std::abs(toCameraDir.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(upHint, toCameraDir)) * halfSize;
    const glm::vec3 up = glm::normalize(glm::cross(toCameraDir, right)) * halfSize;

    appendQuad(triangles, center - right - up, center + right - up, center + right + up, center - right + up,
              handle, highlight);
  }

  void buildPrimitives(gizmo::Frame& frame, const gizmo::State& state, const gizmo::Handle hovered,
                       const std::vector<HandleGeom>& handles, const glm::vec3& center, const gizmo::View& view)
  {
    const float worldLen = worldHandleLength(view, center);
    const float worldPerPixel = worldLen / handleLengthPixels;
    const float halfShaftWidth = shaftPixels * 0.5f * worldPerPixel;
    const glm::vec3 cameraPos = cameraPosition(view);

    const auto highlightFor = [&](const gizmo::Handle h) {
      if (state.dragging && h == state.activeHandle)
      {
        return gizmo::Highlight::active;
      }
      if (!state.dragging && h == hovered)
      {
        return gizmo::Highlight::hovered;
      }
      return gizmo::Highlight::none;
    };

    for (const auto& g : handles)
    {
      const gizmo::Highlight hl = highlightFor(g.handle);

      if (g.handle == gizmo::Handle::uniform)
      {
        appendSquare(frame.triangles, g.center, uniformHandleHalfPixels * worldPerPixel, cameraPos, g.handle, hl);
        continue;
      }

      if (state.mode == gizmo::Mode::rotate)
      {
        for (std::size_t i = 0; i < g.ringPoints.size(); ++i)
        {
          appendShaftQuad(frame.triangles, g.ringPoints[i], g.ringPoints[(i + 1) % g.ringPoints.size()],
                          halfShaftWidth, cameraPos, g.handle, hl);
        }
        continue;
      }

      appendShaftQuad(frame.triangles, g.center, g.tip, halfShaftWidth, cameraPos, g.handle, hl);
      frame.lines.push_back(gizmo::Line{ g.center, g.tip, g.handle, hl });

      if (state.mode == gizmo::Mode::translate)
      {
        appendArrowTip(frame.triangles, g.tip, g.axis, worldPerPixel, cameraPos, g.handle, hl);
      }
      else
      {
        appendSquare(frame.triangles, g.tip, tipHalfWidthPixels * worldPerPixel, cameraPos, g.handle, hl);
      }
    }
  }

  void beginDrag(gizmo::State& state, const gizmo::Input& input, const gizmo::Handle handle,
                const glm::vec3& worldCenter)
  {
    state.dragging = true;
    state.activeHandle = handle;
    state.dragMode = state.mode;
    state.dragSpace = state.space;
    state.dragStartLocal = input.local;
    state.dragStartWorld = input.world;
    state.dragParent = input.parent;
    state.lastResultLocal = input.local;
    state.dragAxis = axisForHandle(state.dragMode, state.dragSpace, handle, input.world.rotation);

    switch (state.dragMode)
    {
      case gizmo::Mode::translate:
      {
        const gizmo::Ray ray = gizmo::mouseRay(input.view, input.mouse);
        state.dragS0 = closestLineParam(worldCenter, state.dragAxis, ray).value_or(0.0f);
        state.dragLastAmount = 0.0f;
        break;
      }
      case gizmo::Mode::rotate:
      {
        const gizmo::Ray ray = gizmo::mouseRay(input.view, input.mouse);
        const auto hit = rayPlaneHit(ray, worldCenter, state.dragAxis);
        state.dragPrevV = hit ? (*hit - worldCenter) : anyPerpendicular(state.dragAxis);
        state.dragTotalAngleDegrees = 0.0f;
        state.dragMousePrev = input.mouse;
        break;
      }
      case gizmo::Mode::scale:
      {
        state.dragMouseStart = input.mouse;

        if (handle == gizmo::Handle::uniform)
        {
          state.dragAxisScreenDir = glm::normalize(glm::vec2(1.0f, -1.0f));
          state.dragAxisScreenLength = handleLengthPixels;
        }
        else
        {
          const float worldLen = worldHandleLength(input.view, worldCenter);
          const glm::vec3 tip = worldCenter + state.dragAxis * worldLen;
          const auto centerScreen = gizmo::project(input.view, worldCenter);
          const auto tipScreen = gizmo::project(input.view, tip);

          if (centerScreen && tipScreen && glm::length(*tipScreen - *centerScreen) > 1e-4f)
          {
            const glm::vec2 diff = *tipScreen - *centerScreen;
            state.dragAxisScreenDir = glm::normalize(diff);
            state.dragAxisScreenLength = glm::length(diff);
          }
          else
          {
            state.dragAxisScreenDir = glm::vec2(1.0f, 0.0f);
            state.dragAxisScreenLength = handleLengthPixels;
          }
        }
        break;
      }
    }
  }

  [[nodiscard]] gizmo::Pose processTranslate(gizmo::State& state, const gizmo::Input& input)
  {
    gizmo::Pose local = state.dragStartLocal;
    const gizmo::Ray ray = gizmo::mouseRay(input.view, input.mouse);
    const auto sOpt = closestLineParam(state.dragStartWorld.position, state.dragAxis, ray);

    float amount;
    if (sOpt)
    {
      amount = *sOpt - state.dragS0;
      state.dragLastAmount = amount;
    }
    else
    {
      amount = state.dragLastAmount;
    }

    if (input.snap.enabled && !input.suppressSnap)
    {
      amount = snapValue(amount, input.snap.translateStep);
    }

    if (isIdentity(state.dragParent))
    {
      local.position = state.dragStartLocal.position + state.dragAxis * amount;
    }
    else
    {
      local.position = worldToLocalPosition(state.dragParent, state.dragStartWorld.position + state.dragAxis * amount,
                                            state.dragStartLocal.position);
    }

    return local;
  }

  [[nodiscard]] gizmo::Pose processRotate(gizmo::State& state, const gizmo::Input& input)
  {
    gizmo::Pose local = state.dragStartLocal;
    const glm::vec3& normal = state.dragAxis;
    const glm::vec3& center = state.dragStartWorld.position;
    const gizmo::Ray ray = gizmo::mouseRay(input.view, input.mouse);
    const float denom = glm::dot(ray.direction, normal);

    if (std::abs(denom) >= edgeOnDotThreshold)
    {
      if (const auto hit = rayPlaneHit(ray, center, normal))
      {
        const glm::vec3 v = *hit - center;

        if (glm::length(v) > 1e-5f)
        {
          const glm::vec3 vn = glm::normalize(v);
          const glm::vec3 prevN = glm::normalize(state.dragPrevV);
          const float cosA = glm::clamp(glm::dot(prevN, vn), -1.0f, 1.0f);
          const float sinA = glm::dot(glm::cross(prevN, vn), normal);
          const float deltaRad = std::atan2(sinA, cosA);

          state.dragTotalAngleDegrees += glm::degrees(deltaRad);
          state.dragPrevV = v;
        }
      }
    }
    else if (const auto centerScreen = gizmo::project(input.view, center))
    {
      const glm::vec2 toMouse = input.mouse - *centerScreen;
      const float len = glm::length(toMouse);
      const glm::vec2 tangent = len > 1e-5f ? glm::vec2(-toMouse.y, toMouse.x) / len : glm::vec2(1.0f, 0.0f);
      const glm::vec2 mouseDelta = input.mouse - state.dragMousePrev;
      const float deltaRad = glm::dot(mouseDelta, tangent) / handleLengthPixels;

      state.dragTotalAngleDegrees += glm::degrees(deltaRad);
    }

    state.dragMousePrev = input.mouse;

    float total = state.dragTotalAngleDegrees;
    if (input.snap.enabled && !input.suppressSnap)
    {
      total = snapValue(total, input.snap.rotateStepDegrees);
    }

    const glm::quat startWorldQuat(glm::radians(state.dragStartWorld.rotation));
    const glm::quat deltaQuat = glm::angleAxis(glm::radians(total), normal);
    const glm::quat newWorldQuat = deltaQuat * startWorldQuat;

    glm::vec3 newWorldEuler = glm::degrees(glm::eulerAngles(newWorldQuat));
    newWorldEuler = gizmo::nearestEquivalentEuler(newWorldEuler, state.dragStartWorld.rotation);

    if (isIdentity(state.dragParent))
    {
      local.rotation = state.dragStartLocal.rotation + (newWorldEuler - state.dragStartWorld.rotation);
      return local;
    }

    if (total == 0.0f)
    {
      return local;
    }

    const glm::quat newLocalQuat = glm::inverse(state.dragParent.orientation) * newWorldQuat;
    const glm::vec3 newLocalEuler = glm::degrees(glm::eulerAngles(newLocalQuat));
    local.rotation = gizmo::nearestEquivalentEuler(newLocalEuler, state.dragStartLocal.rotation);
    return local;
  }

  [[nodiscard]] gizmo::Pose processScale(gizmo::State& state, const gizmo::Input& input)
  {
    gizmo::Pose local = state.dragStartLocal;
    const glm::vec2 delta = input.mouse - state.dragMouseStart;
    float factor = 1.0f + glm::dot(delta, state.dragAxisScreenDir) / state.dragAxisScreenLength;

    if (input.snap.enabled && !input.suppressSnap)
    {
      factor = snapValue(factor, input.snap.scaleStep);
    }

    factor = std::max(factor, minScaleFactor);

    if (state.activeHandle == gizmo::Handle::uniform)
    {
      local.scale = state.dragStartLocal.scale * factor;
    }
    else
    {
      local.scale[axisIndex(state.activeHandle)] *= factor;
    }

    return local;
  }

  [[nodiscard]] gizmo::Pose processDrag(gizmo::State& state, const gizmo::Input& input)
  {
    gizmo::Pose result;

    switch (state.dragMode)
    {
      case gizmo::Mode::translate:
        result = processTranslate(state, input);
        break;
      case gizmo::Mode::rotate:
        result = processRotate(state, input);
        break;
      case gizmo::Mode::scale:
        result = processScale(state, input);
        break;
    }

    if (!isFinite(result))
    {
      return state.lastResultLocal;
    }

    return result;
  }
}

namespace gizmo {
  std::optional<glm::vec2> project(const View& view, const glm::vec3& world)
  {
    if (!viewportIsUsable(view.viewport))
    {
      return std::nullopt;
    }

    const glm::vec4 clip = projectionMatrix(view) * view.view * glm::vec4(world, 1.0f);

    if (clip.w <= clipEpsilon)
    {
      return std::nullopt;
    }

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    const float x = view.viewport.x + (ndc.x * 0.5f + 0.5f) * view.viewport.width;
    const float y = view.viewport.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * view.viewport.height;

    return glm::vec2(x, y);
  }

  Ray mouseRay(const View& view, const glm::vec2& mouse)
  {
    const glm::vec3 origin = cameraPosition(view);

    if (!viewportIsUsable(view.viewport))
    {
      // No pixel-to-ray mapping is meaningful without a real viewport (the aspect ratio and NDC mapping
      // below would divide by zero) - fall back to where the camera is looking rather than a NaN
      // direction, the same way project() reports no screen position at all in this case.
      return Ray{ origin, viewForward(view) };
    }

    const glm::mat4 inv = glm::inverse(projectionMatrix(view) * view.view);
    const float ndcX = ((mouse.x - view.viewport.x) / view.viewport.width) * 2.0f - 1.0f;
    const float ndcY = (1.0f - (mouse.y - view.viewport.y) / view.viewport.height) * 2.0f - 1.0f;

    glm::vec4 farPoint = inv * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    farPoint /= farPoint.w;

    const glm::vec3 direction = glm::normalize(glm::vec3(farPoint) - origin);

    return Ray{ origin, direction };
  }

  glm::vec3 nearestEquivalentEuler(const glm::vec3& eulerDegrees, const glm::vec3& reference)
  {
    const auto nearestAngle = [](const float angle, const float ref) {
      float shifted = angle;
      while (shifted - ref > 180.0f)
      {
        shifted -= 360.0f;
      }
      while (shifted - ref < -180.0f)
      {
        shifted += 360.0f;
      }
      return shifted;
    };

    const auto totalDistance = [&](const glm::vec3& c) {
      return std::abs(c.x - reference.x) + std::abs(c.y - reference.y) + std::abs(c.z - reference.z);
    };

    const glm::vec3 direct(nearestAngle(eulerDegrees.x, reference.x), nearestAngle(eulerDegrees.y, reference.y),
                           nearestAngle(eulerDegrees.z, reference.z));

    const glm::vec3 flipped(nearestAngle(eulerDegrees.x + 180.0f, reference.x),
                            nearestAngle(180.0f - eulerDegrees.y, reference.y),
                            nearestAngle(eulerDegrees.z + 180.0f, reference.z));

    return totalDistance(direct) <= totalDistance(flipped) ? direct : flipped;
  }

  void cancel(State& state)
  {
    state.dragging = false;
    state.activeHandle = Handle::none;
  }

  Frame update(State& state, const Input& input)
  {
    Frame frame;

    const bool viewportOk = viewportIsUsable(input.view.viewport);
    const glm::vec3 worldCenter = input.world.position;
    const auto centerScreen = viewportOk ? project(input.view, worldCenter) : std::nullopt;
    const bool geometryValid = viewportOk && centerScreen.has_value();

    const bool pressedThisFrame = input.mouseDown && !state.mouseDownLastFrame;
    const bool releasedThisFrame = !input.mouseDown && state.mouseDownLastFrame;

    std::vector<HandleGeom> handles;
    if (geometryValid)
    {
      handles = buildHandleGeoms(state.mode, state.space, worldCenter, input.world.rotation, input.view);
    }

    Handle hovered = Handle::none;
    if (geometryValid && input.mouseOverView)
    {
      hovered = hitTest(state.mode, handles, worldCenter, input.view, input.mouse);
    }

    if (!state.dragging && pressedThisFrame && input.mouseOverView && hovered != Handle::none)
    {
      beginDrag(state, input, hovered, worldCenter);
      frame.dragStarted = true;
    }

    const bool draggingThisFrame = state.dragging;
    const Handle draggedHandle = state.activeHandle;

    if (state.dragging)
    {
      const Pose result = processDrag(state, input);
      frame.local = result;
      state.lastResultLocal = result;

      if (releasedThisFrame)
      {
        frame.dragFinished = true;
        state.dragging = false;
        state.activeHandle = Handle::none;
      }
    }

    frame.hovered = draggingThisFrame ? Handle::none : hovered;
    frame.active = draggingThisFrame ? draggedHandle : Handle::none;
    frame.capturesMouse = hovered != Handle::none || draggingThisFrame;

    if (geometryValid)
    {
      buildPrimitives(frame, state, hovered, handles, worldCenter, input.view);
    }

    state.mouseDownLastFrame = input.mouseDown;
    return frame;
  }
}
