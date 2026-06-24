#include "ClientApp.h"
#include <iostream>

int main(int argc, char** argv)
{
  try
  {
    // TODO: parse argv for --host, --port, or --singleplayer (launchLocalServer + project).
    (void)argc;
    (void)argv;

    ClientApp app({ .launchLocalServer = true, .project = "SetupTest.json" });

    app.run();
  }
  catch (const std::exception& e)
  {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
