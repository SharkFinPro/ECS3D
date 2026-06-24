#include <ECS3D.h>
#include <SaveManager.h>
#include <iostream>

int main()
{
  try
  {
    ECS3D ecs;
    ecs.getSaveManager()->loadSaveFile("SetupTest.json");

    while (ecs.isActive())
    {
      ecs.update();
    }
  }
  catch (const std::exception& e)
  {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
