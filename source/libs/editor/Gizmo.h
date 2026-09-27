#ifndef GIZMO_H
#define GIZMO_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <optional>
#include <vector>

// The headless core of the viewport translate/rotate/scale manipulators: hit-testing and drag math over
// world-space line/triangle primitives. No ImGui, no Vulkan, no engine headers - pure glm, so it compiles
// straight into ECS3DTests. It outputs backend-neutral primitives so a native drawing backend can replace
// the ImGui one later with no change here.
//
// Namespaced (unlike most of this codebase) because Mode/Frame/update are too generic to leave global.
namespace gizmo {
  enum class Mode {
    translate,
    rotate,
    scale
  };

  enum class Space {
    world,
    local
  };

  // uniform is the center handle, scale mode only.
  enum class Handle {
    none,
    x,
    y,
    z,
    uniform
  };

  // Screen pixels, y down - ImGui screen space.
  struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
  };

  struct View {
    glm::mat4 view{ 1.0f };   // world -> camera
    float fovDegrees = 45.0f; // vertical fov
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    Rect viewport;
  };

  struct Pose {
    glm::vec3 position{ 0.0f };
    glm::vec3 rotation{ 0.0f }; // Euler degrees
    glm::vec3 scale{ 1.0f };
  };

  struct Snap {
    bool enabled = false;
    float translateStep = 0.0f;
    float rotateStepDegrees = 0.0f;
    float scaleStep = 0.0f;
  };

  struct Input {
    View view;
    glm::vec2 mouse{ 0.0f };     // screen pixels, same space as viewport
    bool mouseDown = false;      // left button held
    bool mouseOverView = false;  // hover and drag start only happen when true; an active drag continues regardless
    bool suppressSnap = false;   // Alt held
    Snap snap;
    Pose local; // the target's Transform local values
    Pose world; // the target's world values (getPosition/getRotation/getScale)
  };

  enum class Highlight {
    none,
    hovered,
    active
  };

  struct Line {
    glm::vec3 a{ 0.0f };
    glm::vec3 b{ 0.0f };
    Handle handle = Handle::none;
    Highlight highlight = Highlight::none;
  };

  struct Triangle {
    glm::vec3 a{ 0.0f };
    glm::vec3 b{ 0.0f };
    glm::vec3 c{ 0.0f };
    Handle handle = Handle::none;
    Highlight highlight = Highlight::none;
  };

  struct Frame {
    std::vector<Line> lines;
    std::vector<Triangle> triangles;
    Handle hovered = Handle::none;
    Handle active = Handle::none;
    bool capturesMouse = false; // hovered or dragging - the caller skips its own click handling
    bool dragStarted = false;   // true on the frame a drag begins
    bool dragFinished = false;  // true on the frame the button is released after a drag
    std::optional<Pose> local;  // the new local pose on every drag frame, including the finishing one
  };

  // A value type the caller keeps alongside its selection - mode/space persist across frames, the rest is
  // drag bookkeeping update() maintains for itself.
  struct State {
    Mode mode = Mode::translate;
    Space space = Space::world;

    bool dragging = false;
    Handle activeHandle = Handle::none;
    bool mouseDownLastFrame = false;

    // Snapshotted from mode/space at beginDrag and used by processDrag for the rest of that drag, so a
    // caller mutating mode/space mid-drag cannot run one mode's math against another's bookkeeping - the
    // change only takes effect on the next drag.
    Mode dragMode = Mode::translate;
    Space dragSpace = Space::world;

    Pose dragStartLocal;
    Pose dragStartWorld;
    Pose lastResultLocal;

    glm::vec3 dragAxis{ 0.0f }; // world-space axis (translate/scale) or plane normal (rotate)

    // translate
    float dragS0 = 0.0f;
    float dragLastAmount = 0.0f;

    // rotate
    glm::vec3 dragPrevV{ 0.0f };
    float dragTotalAngleDegrees = 0.0f;
    glm::vec2 dragMousePrev{ 0.0f };

    // scale
    glm::vec2 dragMouseStart{ 0.0f };
    glm::vec2 dragAxisScreenDir{ 1.0f, 0.0f };
    float dragAxisScreenLength = 1.0f;
  };

  [[nodiscard]] Frame update(State& state, const Input& input);

  // Abandons an active drag without a result - for when the target vanishes mid-drag.
  void cancel(State& state);

  // Exposed for tests and the drawing backend.
  [[nodiscard]] std::optional<glm::vec2> project(const View& view, const glm::vec3& world);

  struct Ray {
    glm::vec3 origin{ 0.0f };
    glm::vec3 direction{ 0.0f, 0.0f, -1.0f };
  };

  [[nodiscard]] Ray mouseRay(const View& view, const glm::vec2& mouse);

  // glm's quat(vec3) composes R = Rz(c)*Ry(b)*Rx(a), so (a, b, c) and (a+180, 180-b, c+180) represent the
  // same orientation; picks whichever of those two is nearest reference, component-wrapped to the nearest
  // multiple of 360 first.
  [[nodiscard]] glm::vec3 nearestEquivalentEuler(const glm::vec3& eulerDegrees, const glm::vec3& reference);
}

#endif //GIZMO_H
