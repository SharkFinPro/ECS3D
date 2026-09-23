using ECS3DNetTransport;
using Xunit;

namespace ECS3DManagedTests;

// The RFC 6455 spec walks through one worked example of the handshake key computation in its section
// 1.3 (Opening Handshake), pairing a known input with its expected output. This class checks the
// engine's implementation against that same pair rather than an invented one, so a mismatch here points
// at an actual spec compliance break rather than an assumption baked into the test itself. The method
// under test lives in the internal surface of the transport assembly and is reached only through the
// InternalsVisibleTo grant declared in the assembly info file alongside it, never through reflection.
public class WebSocketHandshakeTests
{
  [Fact]
  public void ComputeAcceptKey_MatchesTheRfc6455WorkedExample()
  {
    const string key = "dGhlIHNhbXBsZSBub25jZQ==";

    var accept = WebSocketBackend.ComputeAcceptKey(key);

    Assert.Equal("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", accept);
  }

  [Fact]
  public void ComputeAcceptKey_DiffersForADifferentKey()
  {
    // Positive control: without this, the test above could pass merely because the method returns a
    // fixed string regardless of its input.
    var accept = WebSocketBackend.ComputeAcceptKey("a different nonce");

    Assert.NotEqual("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", accept);
  }
}
