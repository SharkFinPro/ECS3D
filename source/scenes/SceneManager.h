#ifndef SCENEMANAGER_H
#define SCENEMANAGER_H

#include <nlohmann/json_fwd.hpp>
#include <memory>
#include <vector>

class ECS3D;
class Scene;

enum class SceneStatus {
  running,
  stopped,
  paused
};

class SceneManager {
public:
  explicit SceneManager(ECS3D* ecs);

  std::shared_ptr<Scene> createScene();

  void loadScene(const std::shared_ptr<Scene>& scene);

  [[nodiscard]] ECS3D* getECS() const;

  void fixedUpdate(float dt) const;

  void variableUpdate();

  [[nodiscard]] nlohmann::json serialize() const;

  void loadFromJSON(const nlohmann::json& scenesData);

  [[nodiscard]] std::shared_ptr<Scene> getCurrentScene() const;

  void startScene();

  void pauseScene();

  void resetScene();

  [[nodiscard]] SceneStatus getSceneStatus() const;

private:
  ECS3D* m_ecs;

  std::vector<std::shared_ptr<Scene>> m_scenes;

  std::vector<std::shared_ptr<Scene>> m_scenesToRemove;

  std::shared_ptr<Scene> m_currentScene = nullptr;

  SceneStatus m_sceneStatus = SceneStatus::stopped;

  void sceneSelector();

  void displayRemoveSceneButton(const std::shared_ptr<Scene>& scene);

  void deleteScenesToRemove();

  [[nodiscard]] uint32_t findValidSceneIndex() const;

  void displaySceneStatusGui();
};



#endif //SCENEMANAGER_H
