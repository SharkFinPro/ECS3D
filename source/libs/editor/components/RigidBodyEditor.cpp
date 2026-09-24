#include "RigidBodyEditor.h"
#include "../ComponentEditor.h"
#include "../GuiComponents.h"
#include "../MixedFields.h"
#include <objects/components/RigidBody.h>
#include <imgui.h>
#include <memory>

void registerRigidBodyEditor(ComponentEditor& componentEditor)
{
  // Keyed by the componentTypeToString display name (what ObjectGUIManager looks the handler up by).
  componentEditor.registerHandler("Rigid Body",
    [](const std::shared_ptr<Component>& component, const MixedFields& mixed) -> bool {
    const auto rigidBody = std::dynamic_pointer_cast<RigidBody>(component);
    if (!rigidBody)
    {
      return false;
    }

    bool edited = false;

    if (ComponentEditor::displayHeader(component))
    {
      bool doGravity = rigidBody->getDoGravity();
      float gravity = rigidBody->getGravity();
      float friction = rigidBody->getFriction();
      float mass = rigidBody->getMass();

      if (gc::accentCheckbox("Do Gravity", &doGravity, mixed.contains("doGravity")))
      {
        rigidBody->setDoGravity(doGravity);
        edited = true;
      }

      if (gc::labeledDrag("Gravity", &gravity, 0.1f, mixed.contains("gravity")))
      {
        rigidBody->setGravity(gravity);
        edited = true;
      }

      if (gc::accentSlider("Friction", &friction, 0.001f, 1.0f, mixed.contains("friction")))
      {
        rigidBody->setFriction(friction);
        edited = true;
      }

      if (gc::accentSlider("Mass", &mass, 1.0f, 50.0f, mixed.contains("mass")))
      {
        rigidBody->setMass(mass);
        edited = true;
      }
    }

    return edited;
  });
}
