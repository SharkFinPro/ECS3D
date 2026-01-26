#ifndef SCENE_H
#define SCENE_H

#include <glm/vec3.hpp>
#include <nlohmann/json_fwd.hpp>
#include <memory>

class AssetManager;
class Component;
class ObjectManager;
class SceneManager;

struct TransformData {
  glm::vec3 position = { 0, 0, 0 };
  glm::vec3 scale = { 1, 1, 1 };
  glm::vec3 rotation = { 0, 0, 0 };
};

class Scene {
public:
  explicit Scene(SceneManager* sceneManager);

  void load() const;

  void update(float dt) const;

  void createBlock(TransformData transformData) const;

  void createRigidBlock(TransformData transformData) const;

  void createSphere(TransformData transformData) const;

  void createPlayer(TransformData transformData) const;

  void createLight(glm::vec3 position, glm::vec3 color, float ambient, float diffuse, float specular) const;

  [[nodiscard]] nlohmann::json serialize() const;

  void loadFromJSON(const nlohmann::json& sceneData) const;

private:
  SceneManager* m_sceneManager;

  std::shared_ptr<AssetManager> m_assetManager;

  std::shared_ptr<ObjectManager> m_objectManager;

  [[nodiscard]] std::shared_ptr<Component> loadComponentFromJSON(const nlohmann::json& componentData) const;
};



#endif //SCENE_H
