#include <VulkanEngine/VulkanEngine.h>
#include <iostream>

int main()
{
  try
  {
    constexpr VulkanEngineOptions vulkanEngineOptions = {
      .WINDOW_WIDTH = 800,
      .WINDOW_HEIGHT = 600,
      .WINDOW_TITLE = "ECS3D"
    };

    VulkanEngine renderer(vulkanEngineOptions);

    while (renderer.isActive())
    {
      renderer.render();
    }
  }
  catch (const std::exception& e)
  {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
