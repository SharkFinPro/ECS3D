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

  [Theory]
  [InlineData(false)]
  [InlineData(true)]
  public void ReadFrame_RestoresTheStreamReadTimeout(bool truncate)
  {
    var frame = TcpBackend.FrameBytes(0x01, new byte[] { 1, 2, 3, 4 });
    var bytes = truncate ? frame[..^2] : frame;

    using var stream = new TimeoutCapableMemoryStream(bytes) { ReadTimeout = 1234 };
    var ok = TcpBackend.ReadFrame(stream, out _, out _);

    Assert.Equal(!truncate, ok);
    Assert.Equal(1234, stream.ReadTimeout);
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
  public void ReadFrame_AcceptsABodyOfExactlyMaxBytesAndRefusesOneMore()
  {
    // The body is the type byte plus the payload, so a 4-byte payload makes bodyLen 5.
    var frame = TcpBackend.FrameBytes(0x01, new byte[] { 1, 2, 3, 4 });

    using var atMax = new MemoryStream(frame);
    Assert.True(TcpBackend.ReadFrame(atMax, out var type, out var payload, maxBytes: 5));
    Assert.Equal(0x01, type);
    Assert.Equal(new byte[] { 1, 2, 3, 4 }, payload);

    using var overMax = new MemoryStream(frame);
    Assert.False(TcpBackend.ReadFrame(overMax, out _, out _, maxBytes: 4));
  }

  [Fact]
  public void ReadFrame_DecodesAFrameWithAnEmptyPayload()
  {
    var frame = TcpBackend.FrameBytes(0x09, Array.Empty<byte>());

    using var stream = new MemoryStream(frame);
    var ok = TcpBackend.ReadFrame(stream, out var type, out var payload);

    Assert.True(ok);
    Assert.Equal(0x09, type);
    Assert.Empty(payload);
  }

  [Fact]
  public void ReadFrame_RefusesAZeroLengthBody()
  {
    using var stream = new MemoryStream(new byte[] { 0, 0, 0, 0 });

    Assert.False(TcpBackend.ReadFrame(stream, out _, out _));
  }

  [Fact]
  public void ReadFrame_GivesUpOnceTheDeadlineHasPassed()
  {
    var frame = TcpBackend.FrameBytes(0x01, new byte[] { 1, 2, 3, 4 });

    // Positive control: the same frame decodes under a deadline that is still ahead.
    using var ahead = new MemoryStream(frame);
    Assert.True(TcpBackend.ReadFrame(ahead, out _, out _, deadline: Environment.TickCount64 + 60000));

    using var expired = new MemoryStream(frame);
    Assert.False(TcpBackend.ReadFrame(expired, out _, out _, deadline: Environment.TickCount64 - 1));
  }

  [Fact]
  public void ReadFrame_GivesUpOnABodyReadOnceTheDeadlineHasPassed()
  {
    // A deadline that expires between the header and the body: the stream hands the header over, then
    // sleeps past the deadline before the body read begins.
    var frame = TcpBackend.FrameBytes(0x01, new byte[] { 1, 2, 3, 4 });
    var deadline = Environment.TickCount64 + 200;

    using var stream = new SlowAfterHeaderStream(frame, 400);
    Assert.False(TcpBackend.ReadFrame(stream, out _, out _, deadline: deadline));
  }

  private sealed class SlowAfterHeaderStream(byte[] bytes, int delayMs) : MemoryStream(bytes)
  {
    public override int Read(Span<byte> buffer)
    {
      var read = base.Read(buffer);
      if (Position == 4)
      {
        System.Threading.Thread.Sleep(delayMs);
      }

      return read;
    }
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
