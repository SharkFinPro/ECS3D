#include "PlayerControllerEditor.h"
#include "../ComponentEditor.h"
#include "../GuiComponents.h"
#include <objects/components/PlayerController.h>
#include <imgui.h>
#include <algorithm>
#include <memory>

void registerPlayerControllerEditor(ComponentEditor& componentEditor)
{
  // Keyed by the componentTypeToString display name (what ObjectGUIManager looks the handler up by).
  componentEditor.registerHandler("Player Controller", [](const std::shared_ptr<Component>& component) -> bool {
    const auto playerController = std::dynamic_pointer_cast<PlayerController>(component);
    if (!playerController)
    {
      return false;
    }

    bool edited = false;

    if (ComponentEditor::displayHeader(component))
    {
      // The player index this object belongs to. The server binds each connection to a slot; a script on
      // this object reads that player's input. A drag field flanked by -/+ steppers, mirroring the
      // collider Layer control.
      int slot = playerController->getPlayerSlot();
      bool slotEdited = false;

      gc::rowLabel("Player Slot");
      ImGui::PushID("PlayerSlot");
      const float step = ImGui::GetFrameHeight();
      const float spacing = ImGui::GetStyle().ItemSpacing.x;
      const float dragWidth = ImGui::GetContentRegionAvail().x - 2.0f * (step + spacing);

      if (ImGui::Button("-", ImVec2(step, step)) && slot > 0)
      {
        --slot;
        slotEdited = true;
      }
      ImGui::SameLine();
      ImGui::SetNextItemWidth(dragWidth);
      if (ImGui::DragInt("##playerSlot", &slot, 0.1f, 0, 255))
      {
        slotEdited = true;
      }
      ImGui::SameLine();
      if (ImGui::Button("+", ImVec2(step, step)))
      {
        ++slot;
        slotEdited = true;
      }
      ImGui::PopID();

      if (slotEdited)
      {
        playerController->setPlayerSlot(std::max(slot, 0));
        edited = true;
      }
    }

    return edited;
  });
}
