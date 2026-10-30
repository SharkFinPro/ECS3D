using System.Collections.Generic;
using System.Threading;

namespace ECS3DNetTransport;

// One connection's outbound messages, in the order they were queued. The tick thread enqueues and the
// connection's own sender thread takes, so a peer that stops reading stalls only its sender thread.
internal sealed class SendQueue(long maxQueuedBytes)
{
  private readonly object _lock = new();
  private readonly Queue<byte[]> _messages = new();
  private long _queuedBytes;
  private bool _closed;

  // False when the queue is already backed up past the cap, which tells the caller to drop the peer. An
  // empty queue accepts any one message, so a single maximum-size snapshot is never refused. A closed
  // queue accepts and discards: the connection is already going away and its own teardown reports that.
  public bool TryEnqueue(byte[] message)
  {
    lock (_lock)
    {
      if (_closed)
      {
        return true;
      }

      if (_messages.Count != 0 && _queuedBytes + message.Length > maxQueuedBytes)
      {
        return false;
      }

      _messages.Enqueue(message);
      _queuedBytes += message.Length;
      Monitor.Pulse(_lock);

      return true;
    }
  }

  // Blocks until a message is available. Null once the queue is closed, even with messages left: a
  // closing connection has no use for them.
  public byte[]? Take()
  {
    lock (_lock)
    {
      while (!_closed && _messages.Count == 0)
      {
        Monitor.Wait(_lock);
      }

      if (_closed)
      {
        return null;
      }

      var message = _messages.Dequeue();
      _queuedBytes -= message.Length;

      return message;
    }
  }

  public void Close()
  {
    lock (_lock)
    {
      _closed = true;
      _messages.Clear();
      _queuedBytes = 0;
      Monitor.PulseAll(_lock);
    }
  }
}
