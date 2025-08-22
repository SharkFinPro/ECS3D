#ifndef ECS3D_H
#define ECS3D_H

#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include <VulkanEngine/VulkanEngine.h>

class SceneManager;
class AssetManager;

class ECS3D {
public:
  ECS3D();

  [[nodiscard]] bool isActive() const;

  void update();

  [[nodiscard]] std::shared_ptr<vke::VulkanEngine> getRenderer() const;

  [[nodiscard]] bool keyIsPressed(int key) const;

  [[nodiscard]] std::shared_ptr<SceneManager> getSceneManager() const;

  [[nodiscard]] std::shared_ptr<AssetManager> getAssetManager() const;

  void logMessage(const std::string& level, const std::string& message);

private:
  std::shared_ptr<vke::VulkanEngine> renderer;

  std::chrono::time_point<std::chrono::steady_clock> previousTime;

  std::shared_ptr<SceneManager> sceneManager;

  std::shared_ptr<AssetManager> assetManager;

  std::vector<std::string> errorMessages;

  void displayMessageLog();

  void initRenderer();

  static void setupImGuiStyle();
};



#endif //ECS3D_H
