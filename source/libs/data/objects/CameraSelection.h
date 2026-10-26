#ifndef CAMERASELECTION_H
#define CAMERASELECTION_H

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <memory>
#include <optional>
#include <string>
#include <uuid.h>

class Object;
class ObjectManager;

struct CameraView {
  glm::vec3 position;
  glm::mat4 view;
  float fov;
  float nearPlane;
  float farPlane;
};

// The first object, in getAllObjects order, with an active Camera and a Transform. When `only` is set,
// no other object is considered. Null when none qualifies, which means the free-fly view.
[[nodiscard]] std::shared_ptr<Object> findActiveCamera(const ObjectManager& objectManager,
                                                       const std::optional<uuids::uuid>& only);

// `eulerDegrees` is the object's rotation, applied to the Camera's own direction. A zero direction
// has no defined forward, so it falls back to (0, 0, -1).
[[nodiscard]] glm::vec3 cameraForward(const glm::vec3& eulerDegrees, const glm::vec3& direction);

// World up, unless forward is parallel to it, where lookAt would degenerate.
[[nodiscard]] glm::vec3 cameraUp(const glm::vec3& forward);

// Empty when the object lacks a Camera or a Transform; does not check whether the Camera is active.
[[nodiscard]] std::optional<CameraView> cameraViewOf(const Object& object);

struct CameraRay {
  glm::vec3 origin;
  glm::vec3 direction;  // unit length
};

// The world-space ray through a pixel of the view the camera renders into. The cursor is in pixels from
// the view's top-left corner. Empty for a view under 1x1 pixel, a cursor outside the view, or a
// non-finite input.
[[nodiscard]] std::optional<CameraRay> cameraRayThrough(const CameraView& camera, float cursorX, float cursorY,
                                                        float viewWidth, float viewHeight);

// How a camera reads in the editor's View combo: the object's name, the player slot when it has a
// PlayerController, and a cue when its Camera is inactive.
[[nodiscard]] std::string cameraLabel(const Object& object);

// What the View combo shows while closed: the chosen camera's label, or `freeFlyLabel` when nothing is
// chosen or the choice is gone.
[[nodiscard]] std::string cameraPreviewLabel(const ObjectManager* objectManager,
                                             const std::optional<uuids::uuid>& chosen, const char* freeFlyLabel);

#endif //CAMERASELECTION_H
