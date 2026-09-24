#include "EditorApp.h"
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <RenderSystem.h>
#include <InputCapture.h>
#include <Replication.h>
#include <Selection.h>
#include <EditorTheme.h>
#include <KeybindDispatcher.h>
#include <NetClient.h>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/imGui/ImGuiInstance.h>
#include <VulkanEngine/components/renderingManager/RenderingManager.h>
#include <VulkanEngine/components/renderingManager/renderer3D/Renderer3D.h>
#include <VulkanEngine/components/renderingManager/renderer3D/MousePicker.h>
#include <objects/Object.h>
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <cstdint>
#include <optional>

void EditorApp::handlePicking()
{
  const auto scene = m_sceneManager->getCurrentScene();
  if (!scene)
  {
    return;
  }

  const auto window = m_renderer->getWindow();
  const bool pressed = window->buttonIsPressed(GLFW_MOUSE_BUTTON_LEFT);

  // Select on a fresh Left-click over the viewport. isSelected() is the renderer's pick result from
  // last frame. Plain click replaces the selection; Ctrl-click adds/removes the picked object instead
  // (a click on empty space still clears, unless Ctrl is held - then it's a no-op).
  const auto mousePicker = m_renderer->getRenderingManager()->getRenderer3D()->getMousePicker();
  if (!m_mouseWasPressed && pressed && mousePicker->canMousePick())
  {
    std::optional<uuids::uuid> picked;
    for (const auto& object : scene->getObjectManager()->getAllObjects())
    {
      if (m_renderSystem->isSelected(object->getUUID()))
      {
        picked = object->getUUID();
        break;
      }
    }

    // io.KeyCtrl (not a raw GLFW key read) so either Ctrl key toggles, matching the tree's own check and
    // KeybindDispatcher's chord matching.
    const bool ctrl = ImGui::GetIO().KeyCtrl;

    // Written directly to the shared selection the object tree/inspector read.
    if (picked.has_value())
    {
      if (ctrl)
      {
        m_selection->toggleObject(picked.value());
      }
      else
      {
        m_selection->selectObject(picked.value());
      }
    }
    else if (!ctrl)
    {
      m_selection->clear();
    }
  }

  m_mouseWasPressed = pressed;
}

input::InputSnapshot EditorApp::captureGatedInput() const
{
  const auto& io = ImGui::GetIO();

  // Don't let editor UI typing drive the game: when ImGui wants the keyboard, report no keys. focused
  // stays true so this view never disables another connected view's input.
  input::InputSnapshot snapshot = input::capture(*m_renderer);

  if (io.WantCaptureKeyboard)
  {
    snapshot.keys.clear();
  }

  // The mouse can't be gated on io.WantCaptureMouse: the viewport is itself an ImGui window, so that
  // flag is set whenever the cursor is over it, which would swallow the right-drag mouse-look a script
  // reads. Forward the mouse only while looking through a scene camera (in free-fly the right-drag is
  // the editor's own camera) and while the scene view is focused.
  const bool mouseDrivesGame = m_viewCameraObject.has_value()
                            && m_renderer->getRenderingManager()->isSceneFocused();

  if (!mouseDrivesGame)
  {
    snapshot.mouseDeltaX = 0.0f;
    snapshot.mouseDeltaY = 0.0f;
    snapshot.scrollY = 0.0f;
    snapshot.buttons = 0;
  }

  return snapshot;
}

bool EditorApp::hasInputChanged(const input::InputSnapshot& snapshot) const
{
  const bool discreteChanged = !m_inputSent
    || snapshot.keys != m_lastInputKeys
    || snapshot.focused != m_lastInputFocused
    || snapshot.buttons != m_lastButtons
    || snapshot.mouseX != m_lastMouseX
    || snapshot.mouseY != m_lastMouseY;

  const bool scrolled = snapshot.scrollY != 0.0f;

  return !m_inputSent || discreteChanged || scrolled;
}

void EditorApp::sendInput()
{
  const input::InputSnapshot snapshot = captureGatedInput();

  if (!hasInputChanged(snapshot))
  {
    return;
  }

  m_lastInputKeys = snapshot.keys;
  m_lastInputFocused = snapshot.focused;
  m_lastButtons = snapshot.buttons;
  m_lastMouseX = snapshot.mouseX;
  m_lastMouseY = snapshot.mouseY;
  m_inputSent = true;

  const auto message = replication::buildInputState(snapshot.focused, snapshot.keys, snapshot.mouseX,
                                                     snapshot.mouseY, snapshot.mouseDeltaX,
                                                     snapshot.mouseDeltaY, snapshot.scrollY, snapshot.buttons);

  m_netClient->send(message);
}

void EditorApp::sendSceneControl(const net::SceneControlOp op) const
{
  net::Message message(net::MessageType::sceneControl);
  message.write(op);
  m_netClient->send(message);
}
