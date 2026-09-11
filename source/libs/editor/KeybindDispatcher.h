#ifndef KEYBINDDISPATCHER_H
#define KEYBINDDISPATCHER_H

#include <Keybinds.h>
#include <VulkanEngine/components/window/Window.h>
#include <functional>
#include <memory>
#include <unordered_map>

namespace vke {
  class VulkanEngine;
}

class KeybindTable;

// Owns the one vke::KeyCallbackEvent listener that resolves key presses against a KeybindTable and
// invokes the registered handler for the matching action. Suppresses dispatch while ImGui owns the
// keyboard, so typing "s" into a text field cannot fire Save.
//
// Also drives the Settings panel's "Rebind" flow: while capturing, the next non-modifier press goes to
// the capture callback instead of being dispatched, and Escape cancels it.
class KeybindDispatcher {
public:
  using ActionHandler = std::function<void()>;
  using CaptureCallback = std::function<void(const KeyChord&)>;

  KeybindDispatcher(std::shared_ptr<vke::VulkanEngine> renderer, std::shared_ptr<KeybindTable> table);

  ~KeybindDispatcher();

  // Registers (or replaces) the handler invoked when the action's bound chord is pressed. An action with
  // no handler is a valid, no-op state - its behavior arrives with a later feature.
  void on(EditorAction action, ActionHandler handler);

  // Enters capture mode: the next non-modifier key press is handed to callback (which is then cleared)
  // instead of being dispatched as an action. Escape cancels capture without invoking it.
  void beginCapture(CaptureCallback callback);

  [[nodiscard]] bool isCapturing() const;

private:
  std::shared_ptr<vke::VulkanEngine> m_renderer;
  std::shared_ptr<KeybindTable> m_table;

  std::unordered_map<EditorAction, ActionHandler> m_handlers;

  CaptureCallback m_captureCallback;

  vke::EventListener<vke::KeyCallbackEvent> m_keyCallbackEventListener;

  void registerWindowEvents();
};

#endif  // KEYBINDDISPATCHER_H
