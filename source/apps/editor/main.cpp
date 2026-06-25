#include "EditorApp.h"
#include <iostream>

int main(int argc, char** argv)
{
  try
  {
    // TODO: parse argv for --project (and optionally --host/--port for the spawned edit server).
    (void)argc;
    (void)argv;

    EditorApp app({ .project = "SetupTest.json" });

    app.run();
  }
  catch (const std::exception& e)
  {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
