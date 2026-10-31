#ifndef KEYBINDREBINDFLOW_H
#define KEYBINDREBINDFLOW_H

#include <Keybinds.h>
#include <SettingsStore.h>
#include <optional>
#include <utility>

// The Settings panel's Keybinds state machine, without any drawing or key capture: which action is waiting
// for a chord, the refusal being shown, and where the list should scroll next. The panel arms the key
// capture itself (KeybindDispatcher) and routes the captured chord back into captureChord.
class KeybindRebindFlow {
public:
  // A refused rebind or reset: the chord `requested` for `action` is already held by `heldBy`. Shown as
  // a modal with no reassign option - displacing the holder would leave it silently unbound.
  // offerChooseAnother is false for a refused reset: there is no in-progress capture to redirect into a
  // new key, so the modal offers only "Go to binding" and a close button.
  struct Conflict {
    EditorAction action;
    EditorAction heldBy;
    KeyChord requested;
    bool offerChooseAnother = true;
  };

  KeybindRebindFlow(KeybindTable& table, SettingsStore& settings)
    : m_table(&table),
      m_settings(&settings)
  {}

  [[nodiscard]] const std::optional<EditorAction>& capturingAction() const
  {
    return m_capturingAction;
  }

  [[nodiscard]] const std::optional<Conflict>& conflict() const
  {
    return m_conflict;
  }

  // The row to scroll to, once: reading it clears it.
  [[nodiscard]] std::optional<EditorAction> takeScrollTarget()
  {
    return std::exchange(m_scrollToAction, std::nullopt);
  }

  void beginCapture(const EditorAction action)
  {
    m_capturingAction = action;
  }

  void cancelCapture()
  {
    m_capturingAction.reset();
  }

  // The chord the capture produced for `action`. A refusal becomes a conflict naming the holding action.
  void captureChord(const EditorAction action, const KeyChord& chord)
  {
    m_capturingAction.reset();

    const auto outcome = m_table->assign(action, chord, *m_settings);
    if (outcome.result == KeybindTable::AssignResult::refused)
    {
      m_conflict = Conflict{ action, *outcome.heldBy, chord };
    }
  }

  void unbind(const EditorAction action)
  {
    m_table->unbind(action, *m_settings);
    m_capturingAction.reset();
  }

  // A refusal becomes a conflict carrying the default chord, with no capture to redirect.
  void reset(const EditorAction action)
  {
    const auto outcome = m_table->reset(action, *m_settings);
    if (outcome.result == KeybindTable::AssignResult::refused)
    {
      m_conflict = Conflict{ action, *outcome.heldBy, *actionInfo(action).defaultChord, false };
    }

    m_capturingAction.reset();
  }

  void resetAll()
  {
    m_table->resetAll(*m_settings);
    m_capturingAction.reset();
  }

  void goToBinding()
  {
    if (m_conflict.has_value())
    {
      m_scrollToAction = m_conflict->heldBy;
      m_conflict.reset();
    }
  }

  void closeConflict()
  {
    m_conflict.reset();
  }

  // Dismisses the conflict and waits for another chord for the same action. Returns that action so the
  // caller can arm the key capture; nullopt when there was no conflict.
  [[nodiscard]] std::optional<EditorAction> chooseAnotherKey()
  {
    if (!m_conflict.has_value())
    {
      return std::nullopt;
    }

    const auto action = m_conflict->action;
    m_conflict.reset();
    m_capturingAction = action;

    return action;
  }

private:
  KeybindTable* m_table;
  SettingsStore* m_settings;

  std::optional<EditorAction> m_capturingAction;
  std::optional<EditorAction> m_scrollToAction;
  std::optional<Conflict> m_conflict;
};

#endif //KEYBINDREBINDFLOW_H
