using System;
using System.Threading.Tasks;
using ECS3DNetTransport;
using Xunit;

namespace ECS3DManagedTests;

public sealed class SendQueueTests
{
  private const int WaitMs = 5000;

  private static byte[] Message(int length, byte fill = 0)
  {
    var message = new byte[length];
    Array.Fill(message, fill);
    return message;
  }

  [Fact]
  public void Take_ReturnsMessagesInTheOrderTheyWereQueued()
  {
    var queue = new SendQueue(1024);
    var first = Message(4, 1);
    var second = Message(4, 2);
    var third = Message(4, 3);

    Assert.True(queue.TryEnqueue(first));
    Assert.True(queue.TryEnqueue(second));
    Assert.True(queue.TryEnqueue(third));

    Assert.Same(first, queue.Take());
    Assert.Same(second, queue.Take());
    Assert.Same(third, queue.Take());
  }

  [Fact]
  public void EmptyQueue_AcceptsOneMessageLargerThanTheCap()
  {
    var queue = new SendQueue(10);
    var big = Message(100);

    Assert.True(queue.TryEnqueue(big));
    Assert.Same(big, queue.Take());
  }

  [Fact]
  public void NonEmptyQueue_RefusesAMessageOverTheCap_AndAcceptsOneOnceATakeFreedRoom()
  {
    var queue = new SendQueue(10);

    Assert.True(queue.TryEnqueue(Message(6)));
    Assert.False(queue.TryEnqueue(Message(6)));
    Assert.True(queue.TryEnqueue(Message(4)));

    Assert.NotNull(queue.Take());

    Assert.True(queue.TryEnqueue(Message(6)));
  }

  [Fact]
  public void Take_BlocksUntilAnotherThreadEnqueues()
  {
    var queue = new SendQueue(1024);
    var message = Message(3, 9);

    var taker = Task.Run(queue.Take);
    Assert.False(taker.Wait(200), "Take returned before anything was queued.");

    Assert.True(queue.TryEnqueue(message));

    Assert.True(taker.Wait(WaitMs), "Take did not wake for the enqueued message.");
    Assert.Same(message, taker.Result);
  }

  [Fact]
  public void Close_WakesABlockedTakeWithNull()
  {
    var queue = new SendQueue(1024);

    var taker = Task.Run(queue.Take);
    Assert.False(taker.Wait(200), "Take returned before the queue was closed.");

    queue.Close();

    Assert.True(taker.Wait(WaitMs), "Take did not wake when the queue closed.");
    Assert.Null(taker.Result);
  }

  [Fact]
  public void Close_DiscardsQueuedMessages_AndIsIdempotent()
  {
    var queue = new SendQueue(1024);
    Assert.True(queue.TryEnqueue(Message(4)));

    queue.Close();
    queue.Close();

    Assert.Null(queue.Take());
  }

  [Fact]
  public void EnqueueAfterClose_ReportsSuccessAndDropsTheMessage()
  {
    var queue = new SendQueue(1);
    queue.Close();

    Assert.True(queue.TryEnqueue(Message(100)));
    Assert.True(queue.TryEnqueue(Message(100)));
    Assert.Null(queue.Take());
  }
}
