#include "PlaySessionHistory.h"
#include <utility>

namespace edits {

EditHistory& PlaySessionHistory::current()
{
  return m_current;
}

const EditHistory& PlaySessionHistory::current() const
{
  return m_current;
}

bool PlaySessionHistory::requestStart()
{
  const bool stopped = m_expected.empty()
    ? m_reported == SceneStatus::stopped
    : m_expected.back() == SceneStatus::stopped;

  if (m_authored.has_value() || !stopped)
  {
    return false;
  }

  m_authored = std::move(m_current);
  m_current = EditHistory();
  m_expected.push_back(SceneStatus::running);
  return true;
}

bool PlaySessionHistory::requestStop()
{
  if (!m_authored.has_value())
  {
    return false;
  }

  m_current = std::move(*m_authored);
  m_authored.reset();
  m_expected.push_back(SceneStatus::stopped);
  return true;
}

bool PlaySessionHistory::observeStatus(const SceneStatus status)
{
  const bool reportedStopped = status == SceneStatus::stopped;

  if (!m_expected.empty())
  {
    if ((m_expected.front() == SceneStatus::stopped) == reportedStopped)
    {
      m_expected.pop_front();
    }

    m_reported = status;
    return false;
  }

  bool swapped = false;

  if (m_reported.has_value() && *m_reported != status)
  {
    if (*m_reported == SceneStatus::stopped)
    {
      m_authored = std::move(m_current);
      m_current = EditHistory();
      swapped = true;
    }
    else if (reportedStopped)
    {
      m_current = m_authored.has_value() ? std::move(*m_authored) : EditHistory();
      m_authored.reset();
      swapped = true;
    }
  }

  m_reported = status;
  return swapped;
}

void PlaySessionHistory::clear()
{
  m_current.clear();
  m_authored.reset();
  m_expected.clear();
}

void PlaySessionHistory::reset()
{
  clear();
  m_reported.reset();
}

bool PlaySessionHistory::hasStash() const
{
  return m_authored.has_value();
}

}
