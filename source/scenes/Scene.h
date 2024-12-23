#ifndef SCENE_H
#define SCENE_H

#include <vector>
#include <memory>
#include <glm/vec3.hpp>

class SceneManager;
class AssetManager;
class ObjectManager;
class Object;
class Light;

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

private:
  SceneManager* sceneManager;
  std::shared_ptr<AssetManager> assetManager;
  std::shared_ptr<ObjectManager> objectManager;
  std::vector<std::shared_ptr<Light>> lights;
};



#endif //SCENE_H
