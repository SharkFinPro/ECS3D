#ifndef ECS3D_H
#define ECS3D_H

#include <uuid.h>
#include <VulkanEngine/VulkanEngine.h>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

class AssetManager;
class SaveManager;
class SceneManager;

class ECS3D {
public:
  ECS3D();

  void prepareForReset();

  void cancelReset();

  void completeReset();

  [[nodiscard]] bool isActive() const;

  void update();

  [[nodiscard]] std::shared_ptr<vke::VulkanEngine> getRenderer() const;

  [[nodiscard]] bool keyIsPressed(int key) const;

  [[nodiscard]] std::shared_ptr<SceneManager> getSceneManager() const;

  [[nodiscard]] std::shared_ptr<AssetManager> getAssetManager() const;

  [[nodiscard]] std::shared_ptr<SaveManager> getSaveManager() const;

  void logMessage(const std::string& level, const std::string& message);

  [[nodiscard]] uuids::uuid createUUID();

private:
  std::shared_ptr<vke::VulkanEngine> m_renderer;

  std::chrono::time_point<std::chrono::steady_clock> m_previousTime;

  std::shared_ptr<SceneManager> m_sceneManager;
  std::shared_ptr<SceneManager> m_previousSceneManager = nullptr;

  std::shared_ptr<AssetManager> m_assetManager;
  std::shared_ptr<AssetManager> m_previousAssetManager = nullptr;

  std::shared_ptr<SaveManager> m_saveManager;

  std::vector<std::string> m_errorMessages;

  std::string m_sceneViewName;

  std::mt19937 m_rng;
  uuids::uuid_random_generator m_uuidGenerator;

  const float m_fixedUpdateDt = 1.0f / 50.0f;
  float m_timeAccumulator = 0.0f;

  void fixedUpdate();

  void variableUpdate();

  void displayMessageLog();

  void initRenderer();

  void displayMenuBar() const;

  void updateDockSpace() const;

  static void setupImGuiStyle();
};



#endif //ECS3D_H
