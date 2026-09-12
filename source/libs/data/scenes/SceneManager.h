#ifndef SCENEMANAGER_H
#define SCENEMANAGER_H

#include <memory>
#include <string>
#include <unordered_map>
#include <uuid.h>

class SceneAsset;

enum class SceneStatus {
  running,
  stopped,
  paused
};

// Data: the scene store + the play/pause/stop state. The app loop drives the systems over
// getCurrentScene()->getObjectManager() when status == running; the Start/Pause/Stop GUI lives in the editor.
class SceneManager {
public:
  void addScene(const std::shared_ptr<SceneAsset>& scene);

  // Drop all scenes (used when (re)loading a project / applying a fresh snapshot, so stale scenes
  // don't linger - addScene is keyed by uuid and won't replace an existing entry).
  void clear();

  [[nodiscard]] std::shared_ptr<SceneAsset> getScene(const uuids::uuid& uuid) const;

  [[nodiscard]] const std::unordered_map<uuids::uuid, std::shared_ptr<SceneAsset>>& getScenes() const;

  // Scenes key their AssetRegistry record off their display name, so two scenes cannot share a name
  // without one becoming unselectable. Returns `desiredName` unchanged if it is free, or if it already
  // belongs to `uuid` itself (so re-registering an already-known scene never renames it); otherwise
  // appends " (2)", " (3)", ... - stripping an existing "(N)" suffix first, so colliding with an
  // already-suffixed name grows the number instead of stacking another one - picking the lowest number
  // not already in use.
  [[nodiscard]] std::string uniqueSceneName(const uuids::uuid& uuid, const std::string& desiredName) const;

  void loadScene(const std::shared_ptr<SceneAsset>& scene);

  [[nodiscard]] std::shared_ptr<SceneAsset> getCurrentScene() const;

  void startScene();

  void pauseScene();

  void resetScene();

  [[nodiscard]] SceneStatus getSceneStatus() const;

private:
  std::unordered_map<uuids::uuid, std::shared_ptr<SceneAsset>> m_scenes;

  std::shared_ptr<SceneAsset> m_currentScene;

  SceneStatus m_sceneStatus = SceneStatus::stopped;
};



#endif //SCENEMANAGER_H
