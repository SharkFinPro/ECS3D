#ifndef SCENEMANAGER_H
#define SCENEMANAGER_H

#include <memory>

class SceneAsset;

enum class SceneStatus {
  running,
  stopped,
  paused
};

class SceneManager {
public:
  void loadScene(const std::shared_ptr<SceneAsset>& scene);

  void updateGui();

  void fixedUpdate(float dt) const;

  void variableUpdate();

  [[nodiscard]] std::shared_ptr<SceneAsset> getCurrentScene() const;

  void startScene();

  void pauseScene();

  void resetScene();

  [[nodiscard]] SceneStatus getSceneStatus() const;

private:
  std::shared_ptr<SceneAsset> m_currentScene;

  SceneStatus m_sceneStatus = SceneStatus::stopped;

  void displaySceneStatusGui();
};



#endif //SCENEMANAGER_H
