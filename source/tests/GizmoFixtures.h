#ifndef GIZMOFIXTURES_H
#define GIZMOFIXTURES_H

#include "Gizmo.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

// Shared setup for GizmoTest.cpp (projection/ray/hover/primitive coverage) and GizmoDragTest.cpp (drag
// math coverage) - header-only so both translation units get the same fixture without a second
// definition of ECS3DTests' own object files.
namespace gizmoFixtures {
  struct GizmoFixture {
    gizmo::View view;
    gizmo::State state;
    gizmo::Input input;

    GizmoFixture()
    {
      view.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
      view.fovDegrees = 60.0f;
      view.nearPlane = 0.1f;
      view.farPlane = 1000.0f;
      view.viewport = gizmo::Rect{ 0.0f, 0.0f, 800.0f, 600.0f };

      input.view = view;
      input.mouseOverView = true;
      input.snap.translateStep = 0.5f;
      input.snap.rotateStepDegrees = 15.0f;
      input.snap.scaleStep = 0.1f;
    }
  };

  [[nodiscard]] inline const gizmo::Line* findLine(const gizmo::Frame& frame, const gizmo::Handle handle)
  {
    for (const auto& line : frame.lines)
    {
      if (line.handle == handle)
      {
        return &line;
      }
    }
    return nullptr;
  }

  [[nodiscard]] inline float pointSegmentDistance(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b)
  {
    const glm::vec2 ab = b - a;
    const float lenSq = glm::dot(ab, ab);
    float t = lenSq > 1e-9f ? glm::dot(p - a, ab) / lenSq : 0.0f;
    t = glm::clamp(t, 0.0f, 1.0f);

    return glm::length(p - (a + ab * t));
  }

  // Magnitude of the rotation a quaternion represents relative to identity, in degrees - sign-agnostic,
  // since most drag tests don't pin down the gizmo's screen-sweep sign convention (RotateSignMatches...
  // in GizmoDragTest.cpp does that separately).
  [[nodiscard]] inline float rotationAngleDegrees(const glm::quat& q)
  {
    return glm::degrees(2.0f * std::acos(glm::clamp(std::abs(q.w), 0.0f, 1.0f)));
  }

  // Presses, drags to `mouse`, and returns the frame produced while the handle at `handleScreenPoint` is
  // held down - shared setup for the drag tests.
  inline gizmo::Frame beginDragOnHandle(GizmoFixture& f, const glm::vec2& handleScreenPoint)
  {
    f.input.mouse = handleScreenPoint;
    gizmo::update(f.state, f.input);
    f.input.mouseDown = true;
    return gizmo::update(f.state, f.input);
  }

  // An axis-aligned camera makes whichever handle points straight down the view axis degenerate (its
  // camera-facing quad's side vector goes to zero) - the primitive tests need every axis to actually
  // render, so they look from an off-axis position instead.
  inline GizmoFixture makeOffAxisFixture()
  {
    GizmoFixture f;
    f.view.view = glm::lookAt(glm::vec3(6.0f, 5.0f, 8.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    f.input.view = f.view;
    return f;
  }

  // The lookAt((0,0,10),(0,0,0),(0,1,0)) fixture camera looks straight down Z, so the X and Y rings (each
  // in a plane that contains the camera) project to lines through the center - the horizontal one (Y) and
  // the vertical one (X) - while only the Z ring, facing the camera, projects to a full circle. A point at
  // exactly 0/90/180/270 degrees around that circle also sits exactly on one of those degenerate lines, so
  // grab the ring at 45 degrees instead, comfortably off both.
  inline constexpr float rotateGrabAngle = 0.25f * glm::pi<float>();

  [[nodiscard]] inline glm::vec2 ringPointScreen(const glm::vec2& centerScreen, const float angle)
  {
    return centerScreen + glm::vec2(100.0f * std::cos(rotateGrabAngle + angle), 100.0f * std::sin(rotateGrabAngle + angle));
  }
}

#endif //GIZMOFIXTURES_H
