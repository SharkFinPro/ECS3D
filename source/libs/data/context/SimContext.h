#ifndef SIMCONTEXT_H
#define SIMCONTEXT_H

#include <string>

// Implemented by ServerApp and handed to the sim systems. UUID generation lives on ObjectManager,
// the fixed timestep is passed straight into fixedUpdate(), and networked input is read from the
// process-global InputState in ECS3DScripting — so logging is all that remains here.
class SimContext {
public:
  virtual ~SimContext() = default;

  virtual void logMessage(const std::string& level, const std::string& message) = 0;
};



#endif //SIMCONTEXT_H
