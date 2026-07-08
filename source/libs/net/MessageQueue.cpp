#include "MessageQueue.h"

namespace net {

void MessageQueue::push(Message message, const int32_t senderId)
{
  // The websocket runs on its own thread, so the inbox/outbox are guarded. This is the
  // thread-safety wrinkle that synchronous script calls don't have.
  std::lock_guard lock(m_mutex);

  m_messages.push(Entry{ std::move(message), senderId });
}

bool MessageQueue::pop(Message& message, int32_t& senderId)
{
  std::lock_guard lock(m_mutex);

  if (m_messages.empty())
  {
    return false;
  }

  message = std::move(m_messages.front().message);
  senderId = m_messages.front().senderId;
  m_messages.pop();

  return true;
}

}
