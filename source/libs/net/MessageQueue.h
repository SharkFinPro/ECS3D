#ifndef MESSAGEQUEUE_H
#define MESSAGEQUEUE_H

#include <Protocol.h>
#include <cstdint>
#include <mutex>
#include <queue>

namespace net {

class MessageQueue {
public:
  // senderId is the stable connection id of the message's origin (server inbox); it is 0 where there is
  // no distinct sender (the client inbox has a single peer).
  void push(Message message, int32_t senderId = 0);

  [[nodiscard]] bool pop(Message& message, int32_t& senderId);

private:
  struct Entry {
    Message message;
    int32_t senderId = 0;
  };

  std::mutex m_mutex;

  std::queue<Entry> m_messages;
};

}



#endif //MESSAGEQUEUE_H
