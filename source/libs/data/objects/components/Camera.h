#ifndef CAMERA_H
#define CAMERA_H

#include "Component.h"
#include <glm/vec3.hpp>

// A view the renderer can look through. Position comes from the object's Transform; the facing
// is this component's own `direction`, applied relative to the Transform rotation (so the camera turns as
// the object turns, but its aim is editable independent of the object's/model's orientation). RenderSystem
// finds the active camera each frame and pushes its pose/params into the vke renderer; a client picks which
// camera to render through via the player<->object association (PlayerController).
class Camera final : public Component {
public:
  Camera();

  [[nodiscard]] glm::vec3 getDirection() const;
  void setDirection(const glm::vec3& direction);

  [[nodiscard]] float getFov() const;
  void setFov(float fov);

  // Each of these reacts only to whatever the *other* plane currently holds, so two calls in different
  // orders are not guaranteed to converge (e.g. setFarPlane before setNearPlane pushes far against the
  // near plane that was active at the time, which setNearPlane afterward does not revisit). loadFromJSON
  // and unpack apply both values atomically through setNearFarPlanes instead, which is order-independent.
  [[nodiscard]] float getNearPlane() const;
  void setNearPlane(float nearPlane);

  [[nodiscard]] float getFarPlane() const;
  void setFarPlane(float farPlane);

  [[nodiscard]] bool isActive() const;
  void setActive(bool active);

  // Outside this range the projection matrix degenerates (fov at or past 180 degrees, or a near/far
  // plane that collapses the frustum). Shared by the editor slider and the setters' own clamps.
  static constexpr float minFovDegrees = 1.0f;
  static constexpr float maxFovDegrees = 179.0f;
  static constexpr float minNearPlane = 0.001f;
  // Far must stay strictly beyond near; this is how far past it far is pushed when the two collide.
  static constexpr float minFarPlaneClearance = 0.001f;

  // The smallest far-plane value that is strictly greater than nearPlane. Past roughly a near of 32768,
  // float's representable spacing exceeds minFarPlaneClearance, so nearPlane + minFarPlaneClearance can
  // round back down to nearPlane itself; this falls back to the next representable float above nearPlane
  // so far > near holds for every finite near. Shared by the component's own clamp and the editor UI so
  // the widget never sends a value the component immediately overrides.
  [[nodiscard]] static float minFarPlaneFor(float nearPlane);

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

  void pack(net::Message& message) const override;

  void unpack(net::MessageReader& messageReader) override;

private:
  // Applies both planes together so the far > near rule reads the same regardless of which of near/far
  // was set last (loadFromJSON and unpack set both, and field order in a save file isn't guaranteed).
  // Non-finite inputs are left at their prior value.
  void setNearFarPlanes(float nearPlane, float farPlane);

  // Local look direction (object space); (0,0,-1) faces the object's forward, matching model orientation.
  glm::vec3 m_direction = glm::vec3(0.0f, 0.0f, -1.0f);
  float m_fov = 45.0f;
  float m_nearPlane = 0.1f;
  float m_farPlane = 1000.0f;
  bool m_active = true;
};



#endif //CAMERA_H
