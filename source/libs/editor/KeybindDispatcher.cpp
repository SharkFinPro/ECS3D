#include "KeybindDispatcher.h"
#include <VulkanEngine/VulkanEngine.h>
#include <imgui.h>
#include <utility>

namespace {
  // Keybinds.cpp hardcodes GLFW's numeric key/mod values so ECS3DSettings never includes GLFW. Asserted
  // here (which does include it, via Window.h) so a future GLFW upgrade that renumbers any of them fails
  // the build instead of silently mis-binding keys.
  static_assert(GLFW_KEY_A == 65);
  static_assert(GLFW_KEY_Z == 90);
  static_assert(GLFW_KEY_0 == 48);
  static_assert(GLFW_KEY_9 == 57);
  static_assert(GLFW_KEY_F1 == 290);
  static_assert(GLFW_KEY_F10 == 299);
  static_assert(GLFW_KEY_F12 == 301);
  static_assert(GLFW_KEY_ESCAPE == 256);
  static_assert(GLFW_KEY_DELETE == 261);
  static_assert(GLFW_KEY_SPACE == 32);
  static_assert(GLFW_MOD_SHIFT == 0x1);
  static_assert(GLFW_MOD_CONTROL == 0x2);
  static_assert(GLFW_MOD_ALT == 0x4);
  static_assert(GLFW_MOD_SUPER == 0x8);
  static_assert(GLFW_KEY_UNKNOWN == -1);

  // Caps Lock (0x10) and Num Lock (0x20) only appear in e.mods when GLFW's lock-key mods are enabled,
  // which vke does not do today, but masking them keeps a stored chord from silently mismatching if
  // that ever changes.
  constexpr int keybindModMask = GLFW_MOD_SHIFT | GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER;

  bool isModifierKey(const int key)
  {
    return key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT || key == GLFW_KEY_LEFT_CONTROL ||
           key == GLFW_KEY_RIGHT_CONTROL || key == GLFW_KEY_LEFT_ALT || key == GLFW_KEY_RIGHT_ALT ||
           key == GLFW_KEY_LEFT_SUPER || key == GLFW_KEY_RIGHT_SUPER;
  }
}

KeybindDispatcher::KeybindDispatcher(std::shared_ptr<vke::VulkanEngine> renderer, std::shared_ptr<KeybindTable> table)
  : m_renderer(std::move(renderer)), m_table(std::move(table))
{
  registerWindowEvents();
}

KeybindDispatcher::~KeybindDispatcher()
{
  if (m_renderer)
  {
    m_renderer->getWindow()->removeListener(m_keyCallbackEventListener);
  }
}

void KeybindDispatcher::on(const EditorAction action, ActionHandler handler)
{
  m_handlers[action] = std::move(handler);
}

void KeybindDispatcher::beginCapture(CaptureCallback callback)
{
  m_captureCallback = std::move(callback);
}

bool KeybindDispatcher::isCapturing() const
{
  return static_cast<bool>(m_captureCallback);
}

void KeybindDispatcher::registerWindowEvents()
{
  // Capture only `this` (not the window shared_ptr): the window owns this listener, so also closing over
  // the renderer would make a window -> listener -> lambda -> window cycle (see SaveUI).
  m_keyCallbackEventListener =
    m_renderer->getWindow()->on<vke::KeyCallbackEvent>([this](const vke::KeyCallbackEvent& e) {
      if (e.action != GLFW_PRESS)
      {
        return;
      }

      if (m_captureCallback)
      {
        if (e.key == GLFW_KEY_ESCAPE)
        {
          m_captureCallback = nullptr;
        }
        else if (e.key >= 0 && !isModifierKey(e.key))
        {
          const auto callback = std::exchange(m_captureCallback, nullptr);
          callback(KeyChord{ e.key, e.mods & keybindModMask });
        }

        return;
      }

      if (ImGui::GetIO().WantCaptureKeyboard)
      {
        return;
      }

      const auto action = m_table->actionFor(KeyChord{ e.key, e.mods & keybindModMask });
      if (!action)
      {
        return;
      }

      if (const auto it = m_handlers.find(*action); it != m_handlers.end() && it->second)
      {
        it->second();
      }
    });
}
