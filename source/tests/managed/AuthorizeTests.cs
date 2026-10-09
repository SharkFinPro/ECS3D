using System;
using System.Text;
using ECS3DNetTransport;
using Xunit;

namespace ECS3DManagedTests;

// TransportBackend.TryAuthorizeHandshake is the shared connection-admission policy both backends run the
// first frame through before adding a connection to the broadcast list. It is internal (along with the
// EditMode/ExpectedToken fields it reads) to ECS3DNetTransport; InternalsVisibleTo in
// Transport/AssemblyInfo.cs exposes them here so the policy is covered directly against a concrete
// backend instance rather than through a real socket. TcpBackend has no side effects until ServerStart is
// called, so a bare `new TcpBackend()` is safe to use as that instance.
//
// The role and frame type bytes match TransportBackend's own (protected) constants: 0 = player,
// 1 = editor, 0xFF = handshake. Any other role is unrecognized.
public class AuthorizeTests
{
  private const byte RolePlayer = 0;
  private const byte RoleEditor = 1;
  private const byte HandshakeType = 0xFF;

  private static byte[] Handshake(byte role, string? token = null)
  {
    if (token is null)
    {
      return new[] { role };
    }

    var tokenBytes = Encoding.UTF8.GetBytes(token);
    var payload = new byte[1 + tokenBytes.Length];
    payload[0] = role;
    Array.Copy(tokenBytes, 0, payload, 1, tokenBytes.Length);
    return payload;
  }

  private static bool Try(TcpBackend backend, byte[] payload, out byte grantedRole, byte type = HandshakeType)
  {
    return backend.TryAuthorizeHandshake(type, payload, out grantedRole);
  }

  [Fact]
  public void TryAuthorizeHandshake_AdmitsAPlayerRegardlessOfEditModeOrToken()
  {
    var backend = new TcpBackend { EditMode = true, ExpectedToken = "secret" };

    Assert.True(Try(backend, Handshake(RolePlayer), out var role));
    Assert.Equal(RolePlayer, role);

    Assert.True(Try(backend, Handshake(RolePlayer, "whatever"), out role));
    Assert.Equal(RolePlayer, role);
  }

  [Fact]
  public void TryAuthorizeHandshake_AdmitsAnEditorEvenWhenTheServerIsNotInEditMode()
  {
    // Not a rejection: an editor is admitted against a play-only server too and simply gets a read-only
    // view there (the server honors no edits from it) - see the doc comment in TransportBackend.cs.
    var backend = new TcpBackend { EditMode = false, ExpectedToken = "" };

    Assert.True(Try(backend, Handshake(RoleEditor), out var role));
    Assert.Equal(RoleEditor, role);
  }

  [Fact]
  public void TryAuthorizeHandshake_AdmitsAnEditorWithNoExpectedTokenInEditMode()
  {
    var backend = new TcpBackend { EditMode = true, ExpectedToken = "" };

    Assert.True(Try(backend, Handshake(RoleEditor), out var role));
    Assert.Equal(RoleEditor, role);
  }

  [Fact]
  public void TryAuthorizeHandshake_AdmitsAnEditorWithTheMatchingTokenInEditMode()
  {
    var backend = new TcpBackend { EditMode = true, ExpectedToken = "secret" };

    Assert.True(Try(backend, Handshake(RoleEditor, "secret"), out var role));
    Assert.Equal(RoleEditor, role);
  }

  [Fact]
  public void TryAuthorizeHandshake_RefusesAnEditorWithTheWrongTokenInEditMode()
  {
    var backend = new TcpBackend { EditMode = true, ExpectedToken = "secret" };

    Assert.False(Try(backend, Handshake(RoleEditor, "wrong"), out _));
  }

  [Fact]
  public void TryAuthorizeHandshake_RefusesATokenThatIsOnlyAPrefixOfTheRealOne()
  {
    var backend = new TcpBackend { EditMode = true, ExpectedToken = "secret" };

    Assert.False(Try(backend, Handshake(RoleEditor, "secre"), out _));
    Assert.True(Try(backend, Handshake(RoleEditor, "secret"), out _));
  }

  [Fact]
  public void TryAuthorizeHandshake_RefusesTheRealTokenWithExtraCharacters()
  {
    var backend = new TcpBackend { EditMode = true, ExpectedToken = "secret" };

    Assert.False(Try(backend, Handshake(RoleEditor, "secret!"), out _));
    Assert.True(Try(backend, Handshake(RoleEditor, "secret"), out _));
  }

  [Fact]
  public void TryAuthorizeHandshake_RefusesAnEditorWithNoTokenWhenOneIsExpected()
  {
    var backend = new TcpBackend { EditMode = true, ExpectedToken = "secret" };

    Assert.False(Try(backend, Handshake(RoleEditor), out _));
    Assert.True(Try(backend, Handshake(RoleEditor, "secret"), out _));
  }

  [Fact]
  public void TryAuthorizeHandshake_RefusesAnEmptyPayload()
  {
    var backend = new TcpBackend { EditMode = true, ExpectedToken = "secret" };

    Assert.False(Try(backend, Array.Empty<byte>(), out _));
    Assert.True(Try(backend, Handshake(RolePlayer), out _));
  }

  [Theory]
  [InlineData((byte)2)]
  [InlineData((byte)0xFF)]
  public void TryAuthorizeHandshake_RefusesAnUnrecognizedRoleByte(byte unknownRole)
  {
    var backend = new TcpBackend { EditMode = true, ExpectedToken = "secret" };

    Assert.False(Try(backend, Handshake(unknownRole), out _));
    Assert.True(Try(backend, Handshake(RolePlayer), out _));
  }

  [Fact]
  public void TryAuthorizeHandshake_RefusesAFrameThatIsNotTheHandshake()
  {
    var backend = new TcpBackend { EditMode = true, ExpectedToken = "secret" };

    Assert.False(Try(backend, Handshake(RolePlayer), out _, type: 0x00));
    Assert.True(Try(backend, Handshake(RolePlayer), out _));
  }

  [Fact]
  public void TryAuthorizeHandshake_ComparesAMultiByteTokenByItsUtf8Bytes()
  {
    var backend = new TcpBackend { EditMode = true, ExpectedToken = "s\u00e9cret" };

    Assert.True(Try(backend, Handshake(RoleEditor, "s\u00e9cret"), out var role));
    Assert.Equal(RoleEditor, role);
    Assert.False(Try(backend, Handshake(RoleEditor, "secret"), out _));
  }
}
