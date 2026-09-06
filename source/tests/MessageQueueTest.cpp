#include <gtest/gtest.h>

#include "MessageQueue.h"

#include <Protocol.h>
#include <algorithm>
#include <atomic>
#include <compare>
#include <cstdint>
#include <thread>
#include <vector>

namespace {
  // Each message carries the sequence number its producer gave it, so a drained message can be traced
  // back to exactly one push. The producer itself is the senderId, which is the field the queue is
  // meant to keep attached.
  net::Message tagged(const uint32_t sequence)
  {
    net::Message message(net::MessageType::inputState);
    message.write(sequence);

    return message;
  }

  uint32_t sequenceOf(const net::Message& message)
  {
    net::MessageReader reader(message);

    return reader.read<uint32_t>();
  }

  struct Delivery {
    int32_t senderId;
    uint32_t sequence;

    // Ordered by producer then sequence, so a sorted drain can be compared against the exact set that
    // was pushed - the defaulted comparison gives both the ordering and the equality that needs.
    auto operator<=>(const Delivery&) const = default;
  };

  // Everything one producer pushes, in the order it pushed it.
  void produce(net::MessageQueue& queue, const int32_t senderId, const uint32_t count)
  {
    for (uint32_t sequence = 0; sequence < count; ++sequence)
    {
      queue.push(tagged(sequence), senderId);
    }
  }

  std::vector<Delivery> expectedDeliveries(const int32_t producers, const uint32_t perProducer)
  {
    std::vector<Delivery> expected;
    expected.reserve(static_cast<std::size_t>(producers) * perProducer);

    for (int32_t producer = 0; producer < producers; ++producer)
    {
      for (uint32_t sequence = 0; sequence < perProducer; ++sequence)
      {
        expected.push_back({ producer, sequence });
      }
    }

    return expected;
  }
}

TEST(MessageQueue, PopReportsAnEmptyQueue)
{
  net::MessageQueue queue;

  net::Message message(net::MessageType::snapshot);
  int32_t senderId = 7;

  EXPECT_FALSE(queue.pop(message, senderId));

  // A failed pop must not have written to either output - a caller that ignores the return value and
  // reads the message anyway should see what it passed in, not a half-assigned entry.
  EXPECT_EQ(message.getType(), net::MessageType::snapshot);
  EXPECT_EQ(senderId, 7);
}

TEST(MessageQueue, PopsInTheOrderPushedAndKeepsEachSenderId)
{
  net::MessageQueue queue;

  queue.push(tagged(0), 11);
  queue.push(tagged(1), 22);
  queue.push(tagged(2), 11);

  for (const auto& expected : { Delivery{ 11, 0 }, Delivery{ 22, 1 }, Delivery{ 11, 2 } })
  {
    net::Message message;
    int32_t senderId = -1;

    ASSERT_TRUE(queue.pop(message, senderId));
    EXPECT_EQ((Delivery{ senderId, sequenceOf(message) }), expected);
  }

  net::Message drained;
  int32_t drainedSender = 0;
  EXPECT_FALSE(queue.pop(drained, drainedSender));
}

TEST(MessageQueue, DefaultsTheSenderIdWhereThereIsOnlyOnePeer)
{
  net::MessageQueue queue;

  // The client inbox has a single peer and pushes without an id, which has to arrive as 0 rather than
  // as whatever the caller's out-parameter happened to hold.
  queue.push(tagged(4));

  net::Message message;
  int32_t senderId = 99;

  ASSERT_TRUE(queue.pop(message, senderId));
  EXPECT_EQ(senderId, 0);
}

TEST(MessageQueue, ConcurrentProducersLoseNothingAndKeepTheirOwnOrder)
{
  constexpr int32_t producers = 4;
  constexpr uint32_t perProducer = 200;

  // A race here is rare per attempt, so the point is the repetition rather than any one round.
  for (int round = 0; round < 25; ++round)
  {
    net::MessageQueue queue;

    std::vector<std::thread> threads;
    threads.reserve(producers);
    for (int32_t producer = 0; producer < producers; ++producer)
    {
      threads.emplace_back(produce, std::ref(queue), producer, perProducer);
    }

    for (auto& thread : threads)
    {
      thread.join();
    }

    std::vector<Delivery> drained;
    std::vector<uint32_t> nextExpected(producers, 0);

    net::Message message;
    int32_t senderId = 0;
    while (queue.pop(message, senderId))
    {
      const auto sequence = sequenceOf(message);

      // One consumer, so the queue's own FIFO order is observable: a producer's messages have to come
      // back in the order it pushed them, whatever order the producers interleaved in.
      ASSERT_GE(senderId, 0);
      ASSERT_LT(senderId, producers);
      ASSERT_EQ(sequence, nextExpected[senderId]);
      ++nextExpected[senderId];

      drained.push_back({ senderId, sequence });
    }

    std::ranges::sort(drained);

    // Nothing lost, nothing duplicated, and no message wearing another producer's id.
    ASSERT_EQ(drained, expectedDeliveries(producers, perProducer));
  }
}

TEST(MessageQueue, ConcurrentProducersAndConsumersDeliverEachMessageExactlyOnce)
{
  constexpr int32_t producers = 4;
  constexpr int consumers = 3;
  constexpr uint32_t perProducer = 200;

  for (int round = 0; round < 25; ++round)
  {
    net::MessageQueue queue;
    std::atomic<bool> producersDone{ false };

    std::vector<std::vector<Delivery>> perConsumer(consumers);

    std::vector<std::thread> consumerThreads;
    consumerThreads.reserve(consumers);
    for (int consumer = 0; consumer < consumers; ++consumer)
    {
      consumerThreads.emplace_back([&queue, &producersDone, &drained = perConsumer[consumer]] {
        while (true)
        {
          net::Message message;
          int32_t senderId = 0;

          if (queue.pop(message, senderId))
          {
            drained.push_back({ senderId, sequenceOf(message) });
            continue;
          }

          // Checked only after a pop came back empty: the producers are joined before this is set, so
          // an empty queue past that point is empty for good. Stopping on the flag rather than on a
          // message count means a lost message fails the assertions below instead of hanging here.
          if (producersDone.load(std::memory_order_acquire))
          {
            return;
          }

          std::this_thread::yield();
        }
      });
    }

    std::vector<std::thread> producerThreads;
    producerThreads.reserve(producers);
    for (int32_t producer = 0; producer < producers; ++producer)
    {
      producerThreads.emplace_back(produce, std::ref(queue), producer, perProducer);
    }

    for (auto& thread : producerThreads)
    {
      thread.join();
    }

    producersDone.store(true, std::memory_order_release);

    for (auto& thread : consumerThreads)
    {
      thread.join();
    }

    std::vector<Delivery> drained;
    for (const auto& consumed : perConsumer)
    {
      drained.insert(drained.end(), consumed.begin(), consumed.end());
    }

    std::ranges::sort(drained);

    // Consumers race each other, so the interleaving is not fixed - what has to hold is that between
    // them they saw every message once, with the sender id it was pushed under.
    ASSERT_EQ(drained, expectedDeliveries(producers, perProducer));
  }
}
