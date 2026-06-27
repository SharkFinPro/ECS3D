#ifndef MESSAGEQUEUE_H
#define MESSAGEQUEUE_H

#include "Protocol.h"
#include <mutex>
#include <queue>

namespace net {

class MessageQueue {
public:
  void push(Message message);

  [[nodiscard]] bool pop(Message& message);

private:
  std::mutex m_mutex;

  std::queue<Message> m_messages;
};

}



#endif //MESSAGEQUEUE_H
