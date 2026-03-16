#ifndef TRANSFORM_H
#define TRANSFORM_H

#include "Component.h"
#include <glm/vec3.hpp>

class ECS3D;

struct TransformBindings
{
  void(*getPosition)(const char* uuid, float* x, float* y, float* z);
  void(*getScale)(const char* uuid, float* x, float* y, float* z);
  void(*getRotation)(const char* uuid, float* x, float* y, float* z);
  void(*setScale)(const char* uuid, float x, float y, float z);
  void(*setRotation)(const char* uuid, float x, float y, float z);
  void(*move)(const char* uuid, float x, float y, float z);
  void(*start)(const char* uuid);
  void(*stop)(const char* uuid);
};

class Transform final : public Component {
public:
  Transform();
  explicit Transform(const glm::vec3& position, const glm::vec3& scale, const glm::vec3& rotation);
  ~Transform() override = default;

  [[nodiscard]] uint8_t getUpdateID() const;

  [[nodiscard]] glm::vec3 getPosition() const;
  [[nodiscard]] glm::vec3 getScale() const;
  [[nodiscard]] glm::vec3 getRotation() const;

  void setScale(glm::vec3 scale);
  void setRotation(glm::vec3 rotation);

  void move(const glm::vec3& direction);

  void displayGui() override;

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

  static void initBindings(ECS3D* ecs);

  [[nodiscard]] static TransformBindings getBindings();

private:
  uint8_t m_updateID = 1;

  ComponentVariable<glm::vec3> m_position = ComponentVariable(glm::vec3(0));
  ComponentVariable<glm::vec3> m_scale = ComponentVariable(glm::vec3(0));
  ComponentVariable<glm::vec3> m_rotation = ComponentVariable(glm::vec3(0));

  [[nodiscard]] static std::shared_ptr<Transform> find(const char* uuid);

  static void bindGetPosition(const char* uuid, float* x, float* y, float* z);
  static void bindGetScale(const char* uuid, float* x, float* y, float* z);
  static void bindGetRotation(const char* uuid, float* x, float* y, float* z);

  static void bindSetScale(const char* uuid, float x, float y, float z);
  static void bindSetRotation(const char* uuid, float x, float y, float z);

  static void bindMove(const char* uuid, float x, float y, float z);

  static void bindStart(const char* uuid);
  static void bindStop(const char* uuid);
};



#endif //TRANSFORM_H
