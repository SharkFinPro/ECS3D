using System;
using System.IO;
using System.Net.WebSockets;
using System.Threading;
using ECS3DNetTransport;
using Xunit;

namespace ECS3DManagedTests;

// WebSocketBackend.ReceiveMessage reassembles one whole WebSocket message (fragmented or not) off a
// server-side WebSocket, enforcing a caller-supplied byte ceiling as it goes. It is internal to
// ECS3DNetTransport; InternalsVisibleTo in Transport/AssemblyInfo.cs exposes it here so this logic is
// covered against a real System.Net.WebSockets.WebSocket built with WebSocket.CreateFromStream, fed
// hand-built client frames, rather than through a real socket.
public class WebSocketReceiveTests
{
  // Read side replays a fixed buffer of client frame bytes; write side just has to accept whatever the
  // server-side WebSocket writes back (e.g. a close response), which these tests never inspect.
  private sealed class FrameStream : Stream
  {
    private readonly MemoryStream _incoming;
    public readonly MemoryStream Outgoing = new();

    public FrameStream(byte[] incomingFrames)
    {
      _incoming = new MemoryStream(incomingFrames);
    }

    public override bool CanRead => true;
    public override bool CanSeek => false;
    public override bool CanWrite => true;
    public override long Length => throw new NotSupportedException();

    public override long Position
    {
      get => throw new NotSupportedException();
      set => throw new NotSupportedException();
    }

    public override int Read(byte[] buffer, int offset, int count) => _incoming.Read(buffer, offset, count);
    public override void Write(byte[] buffer, int offset, int count) => Outgoing.Write(buffer, offset, count);
    public override void Flush() { }
    public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
    public override void SetLength(long value) => throw new NotSupportedException();
  }

  // Builds one RFC 6455 client->server frame. Client frames must be masked (section 5.3); this XORs the
  // payload with a fixed 4-byte key and sets the MASK bit, the same as a real client would.
  private static byte[] BuildFrame(bool fin, byte opcode, byte[] payload)
  {
    using var frame = new MemoryStream();

    frame.WriteByte((byte)((fin ? 0x80 : 0) | opcode));

    if (payload.Length < 126)
    {
      frame.WriteByte((byte)(0x80 | payload.Length));
    }
    else if (payload.Length <= ushort.MaxValue)
    {
      frame.WriteByte(0x80 | 126);
      frame.WriteByte((byte)(payload.Length >> 8));
      frame.WriteByte((byte)payload.Length);
    }
    else
    {
      throw new NotSupportedException("test frames stay under 64 KiB");
    }

    var mask = new byte[] { 0x12, 0x34, 0x56, 0x78 };
    frame.Write(mask, 0, mask.Length);

    var masked = new byte[payload.Length];
    for (var i = 0; i < payload.Length; i++)
    {
      masked[i] = (byte)(payload[i] ^ mask[i % 4]);
    }

    frame.Write(masked, 0, masked.Length);
    return frame.ToArray();
  }

  private static byte[] Concat(params byte[][] parts)
  {
    using var result = new MemoryStream();
    foreach (var part in parts)
    {
      result.Write(part, 0, part.Length);
    }

    return result.ToArray();
  }

  private static WebSocket CreateServerWebSocket(byte[] incomingFrames)
  {
    var stream = new FrameStream(incomingFrames);
    return WebSocket.CreateFromStream(stream, isServer: true, subProtocol: null, keepAliveInterval: TimeSpan.Zero);
  }

  [Fact]
  public void ReceiveMessage_ReturnsASingleUnfragmentedBinaryMessageWhole()
  {
    var payload = new byte[] { 1, 2, 3, 4, 5 };
    using var ws = CreateServerWebSocket(BuildFrame(fin: true, opcode: 0x2, payload));

    var result = WebSocketBackend.ReceiveMessage(ws, CancellationToken.None, maxBytes: 1024);

    Assert.Equal(payload, result);
  }

  [Fact]
  public void ReceiveMessage_ReassemblesAMessageFragmentedAcrossThreeFrames()
  {
    var frames = Concat(
      BuildFrame(fin: false, opcode: 0x2, new byte[] { 1, 2 }),
      BuildFrame(fin: false, opcode: 0x0, new byte[] { 3, 4 }),
      BuildFrame(fin: true, opcode: 0x0, new byte[] { 5, 6 }));
    using var ws = CreateServerWebSocket(frames);

    var result = WebSocketBackend.ReceiveMessage(ws, CancellationToken.None, maxBytes: 1024);

    Assert.Equal(new byte[] { 1, 2, 3, 4, 5, 6 }, result);
  }

  [Fact]
  public void ReceiveMessage_ReassemblesAMessageLargerThanTheInternalReceiveBuffer()
  {
    // ReceiveMessage's own scratch buffer is 8192 bytes, so a single frame bigger than that forces more
    // than one ReceiveAsync call even though it is not fragmented at the WebSocket protocol level.
    var payload = new byte[8192 + 500];
    for (var i = 0; i < payload.Length; i++)
    {
      payload[i] = (byte)(i % 256);
    }

    using var ws = CreateServerWebSocket(BuildFrame(fin: true, opcode: 0x2, payload));

    var result = WebSocketBackend.ReceiveMessage(ws, CancellationToken.None, maxBytes: 1 << 20);

    Assert.Equal(payload, result);
  }

  [Fact]
  public void ReceiveMessage_RefusesAMessageOverMaxBytes()
  {
    var payload = new byte[200];
    using var ws = CreateServerWebSocket(BuildFrame(fin: true, opcode: 0x2, payload));

    var result = WebSocketBackend.ReceiveMessage(ws, CancellationToken.None, maxBytes: 100);

    Assert.Null(result);
  }

  [Fact]
  public void ReceiveMessage_AcceptsAMessageExactlyAtMaxBytes()
  {
    // Positive control for the refusal above: the boundary itself must still be accepted whole.
    var payload = new byte[100];
    for (var i = 0; i < payload.Length; i++)
    {
      payload[i] = (byte)i;
    }

    using var ws = CreateServerWebSocket(BuildFrame(fin: true, opcode: 0x2, payload));

    var result = WebSocketBackend.ReceiveMessage(ws, CancellationToken.None, maxBytes: 100);

    Assert.Equal(payload, result);
  }

  [Fact]
  public void ReceiveMessage_ReturnsNullOnACloseFrame()
  {
    using var ws = CreateServerWebSocket(BuildFrame(fin: true, opcode: 0x8, Array.Empty<byte>()));

    var result = WebSocketBackend.ReceiveMessage(ws, CancellationToken.None, maxBytes: 1024);

    Assert.Null(result);
  }
}
