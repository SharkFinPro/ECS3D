#ifndef TRANSFORM_H
#define TRANSFORM_H

#include "Component.h"
#include <glm/vec3.hpp>

class Transform final : public Component {
public:
  Transform();
  explicit Transform(const glm::vec3& position, const glm::vec3& scale, const glm::vec3& rotation);
  ~Transform() override = default;

  [[nodiscard]] glm::vec3 getPosition() const;
  [[nodiscard]] glm::vec3 getScale() const;
  [[nodiscard]] glm::vec3 getRotation() const;

  // Parent-combined getPosition/Scale/Rotation are for the systems (world transforms); the editor
  // edits this object's OWN local values, so it reads/writes them through these.
  [[nodiscard]] glm::vec3 getLocalPosition() const;
  [[nodiscard]] glm::vec3 getLocalScale() const;
  [[nodiscard]] glm::vec3 getLocalRotation() const;

  // Changes whenever this transform or any ancestor changes, and when this object is reparented. Keys
  // caches of world-space geometry.
  [[nodiscard]] uint64_t getWorldUpdateID() const;

  // A new parent, or an ancestor gaining or losing its Transform, changes the world transform without
  // touching any local value.
  void markReparented();

  void setPosition(glm::vec3 position);
  void setScale(glm::vec3 scale);
  void setRotation(glm::vec3 rotation);

  // Takes a rotation in getRotation's parent-combined terms and stores the local rotation that combines with
  // the parent's into it, so a system that turns a body in world space does not bake the parent's in again.
  void setWorldRotation(glm::vec3 rotation);

  void move(const glm::vec3& direction);

  // The live values these reseed (start from initial, stop back to initial) bypass the setters, so the
  // world stamp needs its own bump here - otherwise a collider's cached mesh/bounding box from the end of
  // the previous run survives into the next.
  void start() override;

  void stop() override;

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

  void pack(net::Message& message) const override;

  void unpack(net::MessageReader& messageReader) override;

private:
  void touch();

  ComponentVariable<glm::vec3> m_position = ComponentVariable(glm::vec3(0));
  ComponentVariable<glm::vec3> m_scale = ComponentVariable(glm::vec3(0));
  ComponentVariable<glm::vec3> m_rotation = ComponentVariable(glm::vec3(0));

  uint64_t m_worldStamp = 0;
};



#endif //TRANSFORM_H
