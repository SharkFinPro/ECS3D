#ifndef CAMERA_H
#define CAMERA_H

#include "Component.h"

// A view the renderer can look through (Phase 4). The camera's pose comes from its object's Transform
// (scripts already drive transforms), so this component only carries projection params + an active flag.
// RenderSystem finds the active camera each frame and pushes its pose/params into the vke renderer; a
// client picks which camera to render through via the player↔object association (PlayerController).
class Camera final : public Component {
public:
  Camera();

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
  float m_fov = 45.0f;
  float m_nearPlane = 0.1f;
  float m_farPlane = 1000.0f;
  bool m_active = true;
};



#endif //CAMERA_H
