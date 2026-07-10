#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "objects/components/Transform.h"
#include "objects/components/RigidBody.h"
#include "objects/components/ModelRenderer.h"
#include "objects/components/LightRenderer.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/SphereCollider.h"
#include "objects/components/Script.h"
#include "objects/components/PlayerController.h"
#include "objects/components/Camera.h"
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

  // Colliders serialize as type "Collider" + a subType; they are keyed here by that subType
  // ("Box"/"Sphere"), which Object::loadFromJSON looks up.
  componentRegistry.registerComponent("Box", [] { return std::make_shared<BoxCollider>(); });
  componentRegistry.registerComponent("Sphere", [] { return std::make_shared<SphereCollider>(); });

  // Script data is just className + a field blob; the live C# instance is attached by ECS3DScripting
  // (server only). loadFromJSON sets the className, so the factory is argless like the rest.
  componentRegistry.registerComponent("Script", [] { return std::make_shared<Script>(); });

  // Player↔object association (Phase 3.2): marks an object as owned by a player slot.
  componentRegistry.registerComponent("PlayerController", [] { return std::make_shared<PlayerController>(); });

  // A view the renderer can look through (Phase 4).
  componentRegistry.registerComponent("Camera", [] { return std::make_shared<Camera>(); });
}
