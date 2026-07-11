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

  [[nodiscard]] float getNearPlane() const;
  void setNearPlane(float nearPlane);

  [[nodiscard]] float getFarPlane() const;
  void setFarPlane(float farPlane);

  [[nodiscard]] bool isActive() const;
  void setActive(bool active);

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

  void pack(net::Message& message) const override;

  void unpack(net::MessageReader& messageReader) override;

private:
  // Local look direction (object space); (0,0,-1) faces the object's forward, matching model orientation.
  glm::vec3 m_direction = glm::vec3(0.0f, 0.0f, -1.0f);
  float m_fov = 45.0f;
  float m_nearPlane = 0.1f;
  float m_farPlane = 1000.0f;
  bool m_active = true;
};



#endif //CAMERA_H
