#ifndef UNSAVEDCHANGES_H
#define UNSAVEDCHANGES_H
#include <cstdint>


// What a request to discard the loaded project should do, given whether it has unsaved edits and whether
// an unsaved-changes prompt is already waiting on an answer.
enum class DiscardDecision {
  performNow,
  ignore,
  prompt
};

[[nodiscard]] inline DiscardDecision decideDiscard(const bool dirty, const bool promptShowing)
{
  if (!dirty)
  {
    return DiscardDecision::performNow;
  }

  // A second request must not steal the pending one's answer.
  if (promptShowing)
  {
    return DiscardDecision::ignore;
  }

  return DiscardDecision::prompt;
}

// The project is clean exactly when it matches what was last saved, loaded or created. The undo history's
// position (EditHistory::position) stands for the edits that can be undone, so undoing back to the save
// point reads clean. Two things have no reverse and keep the project dirty until the next save: an edit
// outside the history, and a value edit still in progress (its commit is what records it).
class UnsavedChanges {
public:
  using HistoryPosition = std::uint64_t;

  // An edit the history does not hold.
  void markEdited()
  {
    m_editedOutsideHistory = true;
  }

  void beginUncommittedEdit()
  {
    m_uncommittedEdit = true;
  }

  void endUncommittedEdit()
  {
    m_uncommittedEdit = false;
  }

  void markSaved(const HistoryPosition position)
  {
    m_savedPosition = position;
    m_editedOutsideHistory = false;
    m_uncommittedEdit = false;
  }

  [[nodiscard]] bool isDirty(const HistoryPosition position) const
  {
    return m_editedOutsideHistory || m_uncommittedEdit || position != m_savedPosition;
  }

private:
  HistoryPosition m_savedPosition = 0;
  bool m_editedOutsideHistory = false;
  bool m_uncommittedEdit = false;
};

#endif //UNSAVEDCHANGES_H
