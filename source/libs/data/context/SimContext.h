#ifndef SIMCONTEXT_H
#define SIMCONTEXT_H

#include <string>

// The seam that replaces the old getOwner()->getManager()->getECS() god-object chain for the sim
// systems: the app (ServerApp) implements it and hands it to the systems. The other services the
// migration once expected to route through here ended up owned closer to the data — uuid generation
// lives on ObjectManager, the fixed timestep is passed straight into fixedUpdate(), and networked
// input is read from the process-global InputState in ECS3DScripting — so logging is all that remains.
class SimContext {
public:
  virtual ~SimContext() = default;

  virtual void logMessage(const std::string& level, const std::string& message) = 0;
};



#endif //SIMCONTEXT_H
