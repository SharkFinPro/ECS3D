#ifndef SCENE_H
#define SCENE_H

#include <vector>
#include <memory>
#include <glm/vec3.hpp>

class SceneManager;
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

  void createBlock(TransformData transformData, std::shared_ptr<Object>* object = nullptr) const;

  void createRigidBlock(TransformData transformData, std::shared_ptr<Object>* object = nullptr) const;

  void createSphere(TransformData transformData, std::shared_ptr<Object>* object = nullptr) const;

  void createPlayer(TransformData transformData, std::shared_ptr<Object>* object = nullptr) const;

private:
  SceneManager* sceneManager;
  std::shared_ptr<ObjectManager> objectManager;
  std::vector<std::shared_ptr<Light>> lights;

  static void displayLightGui(const std::shared_ptr<Light>& light, int id);
};



#endif //SCENE_H
