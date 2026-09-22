using ECS3DNetTransport;
using Xunit;

namespace ECS3DManagedTests;

// RFC 6455 section 1.3's worked example: a known Sec-WebSocket-Key must produce this exact
// Sec-WebSocket-Accept value. WebSocketBackend.ComputeAcceptKey is internal to ECS3DNetTransport;
// InternalsVisibleTo in Transport/AssemblyInfo.cs exposes it here so the handshake math is covered
// directly rather than through a real socket.
public class WebSocketHandshakeTests
{
  [Fact]
  public void ComputeAcceptKey_MatchesTheRfc6455WorkedExample()
  {
    const string key = "dGhlIHNhbXBsZSBub25jZQ==";

    var accept = WebSocketBackend.ComputeAcceptKey(key);

    Assert.Equal("s3pPLMBiTxaQ9kYGj5Qe7Ooo2pQ=", accept);
  }

  [Fact]
  public void ComputeAcceptKey_DiffersForADifferentKey()
  {
    // Positive control: without this, the test above could pass merely because the method returns a
    // fixed string regardless of its input.
    var accept = WebSocketBackend.ComputeAcceptKey("a different nonce");

    Assert.NotEqual("s3pPLMBiTxaQ9kYGj5Qe7Ooo2pQ=", accept);
  }
}
