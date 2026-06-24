#ifndef SIMCONTEXT_H
#define SIMCONTEXT_H

#include <string>

class SimContext {
public:
  virtual ~SimContext() = default;

  // TODO: expose the role-scoped services the sim systems need (physics, collisions, scripts)
  // TODO:   without the ECS3D god object / getOwner()->getManager()->getECS() chain. Likely:
  // TODO:   logMessage, createUUID, networked InputState lookups, and the fixed timestep.

  virtual void logMessage(const std::string& level, const std::string& message) = 0;
};



#endif //SIMCONTEXT_H
