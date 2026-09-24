using System;
using System.IO;
using System.Text;
using ECS3DNetTransport;
using Xunit;

namespace ECS3DManagedTests;

// WebSocketBackend.ReadHttpHeaders/PerformServerHandshake perform the RFC 6455 opening handshake by hand
// (see the class comment in WebSocketBackend.cs for why: binding IPAddress.Any without an HttpListener URL
// reservation). Both are internal to ECS3DNetTransport; InternalsVisibleTo in Transport/AssemblyInfo.cs
// exposes them here so this logic is covered directly rather than through a real socket.
public class WebSocketUpgradeTests
{
  // A duplex in-memory stream: reads come from a fixed input buffer, writes land in Written for
  // inspection. Neither ReadHttpHeaders nor PerformServerHandshake touch Stream.ReadTimeout, so unlike
  // TimeoutCapableMemoryStream this needs no timeout bookkeeping.
  private sealed class DuplexTestStream : Stream
  {
    private readonly MemoryStream _input;
    public readonly MemoryStream Written = new();

    public DuplexTestStream(byte[] input)
    {
      _input = new MemoryStream(input);
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

    public override int Read(byte[] buffer, int offset, int count) => _input.Read(buffer, offset, count);
    public override void Write(byte[] buffer, int offset, int count) => Written.Write(buffer, offset, count);
    public override void Flush() { }
    public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
    public override void SetLength(long value) => throw new NotSupportedException();
  }

  // -- ReadHttpHeaders --

  [Fact]
  public void ReadHttpHeaders_ReturnsLinesUpToTheBlankLine_Crlf()
  {
    var input = Encoding.ASCII.GetBytes("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
    using var stream = new DuplexTestStream(input);

    var lines = WebSocketBackend.ReadHttpHeaders(stream);

    Assert.NotNull(lines);
    Assert.Equal(new[] { "GET / HTTP/1.1", "Host: example.com" }, lines!);
  }

  [Fact]
  public void ReadHttpHeaders_ReturnsLinesUpToTheBlankLine_BareLf()
  {
    // The code strips '\r' and terminates a line on '\n' alone, so a peer sending bare LF must work too.
    var input = Encoding.ASCII.GetBytes("GET / HTTP/1.1\nHost: example.com\n\n");
    using var stream = new DuplexTestStream(input);

    var lines = WebSocketBackend.ReadHttpHeaders(stream);

    Assert.NotNull(lines);
    Assert.Equal(new[] { "GET / HTTP/1.1", "Host: example.com" }, lines!);
  }

  [Fact]
  public void ReadHttpHeaders_LeavesTheStreamAtTheFirstByteAfterTheBlankLine()
  {
    var frameBytes = new byte[] { 0xAA, 0xBB, 0xCC };
    var input = Concat(
      Encoding.ASCII.GetBytes("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n"),
      frameBytes);
    using var stream = new DuplexTestStream(input);

    var lines = WebSocketBackend.ReadHttpHeaders(stream);
    Assert.NotNull(lines);

    var readBack = new byte[frameBytes.Length];
    var n = stream.Read(readBack, 0, readBack.Length);

    Assert.Equal(frameBytes.Length, n);
    Assert.Equal(frameBytes, readBack);
  }

  [Fact]
  public void ReadHttpHeaders_ReturnsNullOnEofBeforeTheBlankLine()
  {
    var input = Encoding.ASCII.GetBytes("GET / HTTP/1.1\r\nHost: example.com\r\n");
    using var stream = new DuplexTestStream(input);

    var lines = WebSocketBackend.ReadHttpHeaders(stream);

    Assert.Null(lines);
  }

  [Fact]
  public void ReadHttpHeaders_AcceptsALineAtExactlyTheLengthLimit()
  {
    // Positive control for the over-limit case below: the code checks `line.Length > 8192` after
    // appending each char, so a line of exactly 8192 chars never trips it.
    var line = new string('a', 8192);
    var input = Encoding.ASCII.GetBytes($"{line}\r\n\r\n");
    using var stream = new DuplexTestStream(input);

    var lines = WebSocketBackend.ReadHttpHeaders(stream);

    Assert.NotNull(lines);
    Assert.Equal(new[] { line }, lines!);
  }

  [Fact]
  public void ReadHttpHeaders_ReturnsNullOnceALineExceedsTheLengthLimit()
  {
    // One char past the limit: the check runs right after that char is appended, before any terminator
    // is seen, so no trailing CRLF is needed to trigger it.
    var line = new string('a', 8193);
    var input = Encoding.ASCII.GetBytes(line);
    using var stream = new DuplexTestStream(input);

    var lines = WebSocketBackend.ReadHttpHeaders(stream);

    Assert.Null(lines);
  }

  [Fact]
  public void ReadHttpHeaders_AcceptsExactlyOneHundredLines()
  {
    // Positive control for the over-limit case below: `lines.Count > 100` is only checked once a new
    // line starts, so 100 completed lines followed by the terminating blank line must still succeed.
    var sb = new StringBuilder();
    for (var i = 0; i < 100; i++)
    {
      sb.Append($"H{i}: v\r\n");
    }

    sb.Append("\r\n");
    using var stream = new DuplexTestStream(Encoding.ASCII.GetBytes(sb.ToString()));

    var lines = WebSocketBackend.ReadHttpHeaders(stream);

    Assert.NotNull(lines);
    Assert.Equal(100, lines!.Count);
  }

  [Fact]
  public void ReadHttpHeaders_ReturnsNullOnceMoreThanOneHundredLinesStart()
  {
    // 101 completed lines, then one more char: `lines.Count > 100` (101 > 100) trips on that char, before
    // a 102nd line is ever completed.
    var sb = new StringBuilder();
    for (var i = 0; i < 101; i++)
    {
      sb.Append($"H{i}: v\r\n");
    }

    sb.Append('X');
    using var stream = new DuplexTestStream(Encoding.ASCII.GetBytes(sb.ToString()));

    var lines = WebSocketBackend.ReadHttpHeaders(stream);

    Assert.Null(lines);
  }

  // -- PerformServerHandshake --

  // The RFC 6455 worked example from WebSocketHandshakeTests: a known Sec-WebSocket-Key paired with its
  // spec-verified Sec-WebSocket-Accept, so the expected response here is not re-derived from memory.
  private const string RfcKey = "dGhlIHNhbXBsZSBub25jZQ==";
  private const string RfcAccept = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";

  [Fact]
  public void PerformServerHandshake_AcceptsAValidUpgradeAndWritesTheExactResponse()
  {
    var request = Encoding.ASCII.GetBytes(
      "GET / HTTP/1.1\r\n" +
      "Host: example.com\r\n" +
      "Upgrade: websocket\r\n" +
      "Connection: Upgrade\r\n" +
      $"Sec-WebSocket-Key: {RfcKey}\r\n" +
      "Sec-WebSocket-Version: 13\r\n" +
      "\r\n");
    using var stream = new DuplexTestStream(request);

    var ok = WebSocketBackend.PerformServerHandshake(stream);

    Assert.True(ok);

    var expected =
      "HTTP/1.1 101 Switching Protocols\r\n" +
      "Upgrade: websocket\r\n" +
      "Connection: Upgrade\r\n" +
      $"Sec-WebSocket-Accept: {RfcAccept}\r\n\r\n";
    Assert.Equal(expected, Encoding.ASCII.GetString(stream.Written.ToArray()));
  }

  [Fact]
  public void PerformServerHandshake_MatchesTheHeaderNameCaseInsensitively()
  {
    var request = Encoding.ASCII.GetBytes(
      "GET / HTTP/1.1\r\n" +
      $"sec-websocket-key: {RfcKey}\r\n" +
      "\r\n");
    using var stream = new DuplexTestStream(request);

    var ok = WebSocketBackend.PerformServerHandshake(stream);

    Assert.True(ok);
    Assert.Contains($"Sec-WebSocket-Accept: {RfcAccept}", Encoding.ASCII.GetString(stream.Written.ToArray()));
  }

  [Fact]
  public void PerformServerHandshake_RefusesARequestWithNoKeyAndWritesNothing()
  {
    var request = Encoding.ASCII.GetBytes("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
    using var stream = new DuplexTestStream(request);

    var ok = WebSocketBackend.PerformServerHandshake(stream);

    Assert.False(ok);
    Assert.Equal(0, stream.Written.Length);
  }

  [Fact]
  public void PerformServerHandshake_RefusesAnEmptyKeyAndWritesNothing()
  {
    var request = Encoding.ASCII.GetBytes("GET / HTTP/1.1\r\nSec-WebSocket-Key: \r\n\r\n");
    using var stream = new DuplexTestStream(request);

    var ok = WebSocketBackend.PerformServerHandshake(stream);

    Assert.False(ok);
    Assert.Equal(0, stream.Written.Length);
  }

  private static byte[] Concat(byte[] a, byte[] b)
  {
    var result = new byte[a.Length + b.Length];
    Array.Copy(a, result, a.Length);
    Array.Copy(b, 0, result, a.Length, b.Length);
    return result;
  }
}
