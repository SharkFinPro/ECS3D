#ifndef ECS3D_H
#define ECS3D_H

#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include <VulkanEngine/VulkanEngine.h>

class AssetManager;
class SaveManager;
class SceneManager;

class ECS3D {
public:
  ECS3D();

  void reset();

  [[nodiscard]] bool isActive() const;

  void update();

  [[nodiscard]] std::shared_ptr<vke::VulkanEngine> getRenderer() const;

  [[nodiscard]] bool keyIsPressed(int key) const;

  [[nodiscard]] std::shared_ptr<SceneManager> getSceneManager() const;

  [[nodiscard]] std::shared_ptr<AssetManager> getAssetManager() const;

  [[nodiscard]] std::shared_ptr<SaveManager> getSaveManager() const;

  void logMessage(const std::string& level, const std::string& message);

private:
  std::shared_ptr<vke::VulkanEngine> m_renderer;

  std::chrono::time_point<std::chrono::steady_clock> m_previousTime;

  std::shared_ptr<SceneManager> m_sceneManager;

  std::shared_ptr<AssetManager> m_assetManager;

  std::shared_ptr<SaveManager> m_saveManager;

  std::vector<std::string> m_errorMessages;

  void displayMessageLog();

  void initRenderer();

  void displayMenuBar() const;

  static void setupImGuiStyle();
};



#endif //ECS3D_H
