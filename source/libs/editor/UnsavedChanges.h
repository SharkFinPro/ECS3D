#ifndef UNSAVEDCHANGES_H
#define UNSAVEDCHANGES_H

#include <cstddef>

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

// Dirty whenever the edit count differs from the count snapshotted at the last save/load/new - the
// project has no version counter of its own to compare instead.
class UnsavedChanges {
public:
  void markEdited()
  {
    ++m_editCount;
  }

  void markSaved()
  {
    m_savedEditCount = m_editCount;
  }

  [[nodiscard]] bool isDirty() const
  {
    return m_editCount != m_savedEditCount;
  }

private:
  std::size_t m_editCount = 0;
  std::size_t m_savedEditCount = 0;
};

#endif //UNSAVEDCHANGES_H
