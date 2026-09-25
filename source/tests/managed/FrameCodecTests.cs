using System;
using System.IO;
using ECS3DNetTransport;
using Xunit;

namespace ECS3DManagedTests;

// The raw-TCP wire framing: a 4-byte big-endian length (covering the type byte plus payload) followed
// by the bytes. TcpBackend.FrameBytes/ReadFrame are internal to ECS3DNetTransport; InternalsVisibleTo in
// Transport/AssemblyInfo.cs exposes them here so this logic is covered directly rather than through a
// real socket.
public class FrameCodecTests
{
  [Fact]
  public void FrameBytes_EncodesTypeAndPayloadWithBigEndianLength()
  {
    var payload = new byte[] { 0x10, 0x20, 0x30 };

    var frame = TcpBackend.FrameBytes(0x07, payload);

    // Length is 1 (type) + 3 (payload) = 4, written big-endian, followed by the type byte and payload.
    Assert.Equal(new byte[] { 0, 0, 0, 4, 0x07, 0x10, 0x20, 0x30 }, frame);
  }

  [Fact]
  public void FrameBytes_EncodesAnEmptyPayload()
  {
    var frame = TcpBackend.FrameBytes(0x01, Array.Empty<byte>());

    Assert.Equal(new byte[] { 0, 0, 0, 1, 0x01 }, frame);
  }

  [Fact]
  public void ReadFrame_RoundTripsWhatFrameBytesWrote()
  {
    var payload = new byte[] { 0xAA, 0xBB, 0xCC, 0xDD };
    var frame = TcpBackend.FrameBytes(0x42, payload);

    // A timeout-capable stream exercises the path where ReadBody applies and restores ReadTimeout.
    using var stream = new TimeoutCapableMemoryStream(frame);
    var ok = TcpBackend.ReadFrame(stream, out var type, out var decodedPayload);

    Assert.True(ok);
    Assert.Equal(0x42, type);
    Assert.Equal(payload, decodedPayload);
  }

  [Fact]
  public void ReadFrame_ReadsFromAStreamThatCannotTimeOut()
  {
    var payload = new byte[] { 0xAA, 0xBB, 0xCC, 0xDD };
    var frame = TcpBackend.FrameBytes(0x42, payload);

    using var stream = new MemoryStream(frame);
    Assert.False(stream.CanTimeout);
    var ok = TcpBackend.ReadFrame(stream, out var type, out var decodedPayload);

    Assert.True(ok);
    Assert.Equal(0x42, type);
    Assert.Equal(payload, decodedPayload);
  }

  [Fact]
  public void ReadFrame_RefusesAFrameOverMaxBytes()
  {
    // A frame whose declared body length exceeds maxBytes must be refused outright, not decoded and
    // handed back - this is the ceiling TransportBackend.MaxMessageBytes/MaxHandshakeBytes rely on to
    // keep a peer's declared length from being an unbounded allocation.
    var frame = TcpBackend.FrameBytes(0x01, new byte[10]);

    using var stream = new MemoryStream(frame);
    var ok = TcpBackend.ReadFrame(stream, out _, out _, maxBytes: 5);

    Assert.False(ok);
  }

  [Fact]
  public void ReadFrame_FailsOnATruncatedStream()
  {
    // Positive control for the two tests above: a stream that ends before the declared body length is
    // satisfied must fail rather than silently returning a short or garbage payload.
    var frame = TcpBackend.FrameBytes(0x01, new byte[] { 1, 2, 3, 4 });
    var truncated = frame[..^2];

    // The header is intact so ReadFrame reaches the body read, with the timeout applied.
    using var stream = new TimeoutCapableMemoryStream(truncated);
    var ok = TcpBackend.ReadFrame(stream, out _, out _);

    Assert.False(ok);
  }
}
