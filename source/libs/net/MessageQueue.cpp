#include "MessageQueue.h"

namespace net {

void MessageQueue::push(Message message)
{
  // The websocket runs on its own thread, so the inbox/outbox are guarded. This is the
  // thread-safety wrinkle that synchronous script calls don't have.
  std::lock_guard lock(m_mutex);

  m_messages.push(std::move(message));
}

bool MessageQueue::pop(Message& message)
{
  std::lock_guard lock(m_mutex);

  if (m_messages.empty())
  {
    return false;
  }

  message = std::move(m_messages.front());
  m_messages.pop();

  return true;
}

}
