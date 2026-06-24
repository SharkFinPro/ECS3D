#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "objects/components/Transform.h"
#include "objects/components/RigidBody.h"
#include "objects/components/ModelRenderer.h"
#include "objects/components/LightRenderer.h"
#include <memory>

void registerDataComponents(ComponentRegistry& componentRegistry)
{
  // Every component's DATA factory is registered here, in ECS3DData, because the data classes live
  // here and the data is needed everywhere (the server replicates it even when it doesn't render it).
  // Only the systems differ per app, not which component data exists.
  componentRegistry.registerComponent("Transform", [] { return std::make_shared<Transform>(); });
  componentRegistry.registerComponent("RigidBody", [] { return std::make_shared<RigidBody>(); });
  componentRegistry.registerComponent("ModelRenderer", [] { return std::make_shared<ModelRenderer>(); });
  componentRegistry.registerComponent("LightRenderer", [] { return std::make_shared<LightRenderer>(); });

  // TODO: register the remaining component data factories as they migrate: Box/Sphere Collider
  // TODO:   (keyed by subType), and Script (with the server's ScriptManager injected by ECS3DScripting).
}
