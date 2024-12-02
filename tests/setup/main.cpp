#include "source/ECS3D.h"
#include <iostream>

int main()
{
  try
  {
    ECS3D ecs;

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
