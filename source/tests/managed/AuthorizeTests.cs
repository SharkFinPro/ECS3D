using System;
using System.Text;
using ECS3DNetTransport;
using Xunit;

namespace ECS3DManagedTests;

// TransportBackend.Authorize is the shared connection-admission policy both backends run the handshake
// payload through before adding a connection to the broadcast list. It is internal (along with the
// EditMode/ExpectedToken fields it reads) to ECS3DNetTransport; InternalsVisibleTo in
// Transport/AssemblyInfo.cs exposes them here so the policy is covered directly against a concrete
// backend instance rather than through a real socket. TcpBackend has no side effects until ServerStart is
// called, so a bare `new TcpBackend()` is safe to use as that instance.
//
// Role bytes match TransportBackend's own (private) constants: 0 = player, 1 = editor. Anything else is
// an unrecognized role.
public class AuthorizeTests
{
  private const byte RolePlayer = 0;
  private const byte RoleEditor = 1;
  private const byte RoleUnknown = 2;

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

  [Fact]
  public void Authorize_AcceptsAPlayerRegardlessOfEditModeOrToken()
  {
    var backend = new TcpBackend { EditMode = true, ExpectedToken = "secret" };

    Assert.True(backend.Authorize(Handshake(RolePlayer)));
  }

  [Fact]
  public void Authorize_AdmitsAnEditorEvenWhenTheServerIsNotInEditMode()
  {
    // Not a rejection: an editor is admitted against a play-only server too and simply gets a read-only
    // view there (the server honors no edits from it) - see the Authorize doc comment in
    // TransportBackend.cs. Only role == editor's token check is gated on EditMode.
    var backend = new TcpBackend { EditMode = false, ExpectedToken = "" };

    Assert.True(backend.Authorize(Handshake(RoleEditor)));
  }

  [Fact]
  public void Authorize_AcceptsAnEditorWithTheMatchingTokenInEditMode()
  {
    var backend = new TcpBackend { EditMode = true, ExpectedToken = "secret" };

    Assert.True(backend.Authorize(Handshake(RoleEditor, "secret")));
  }

  [Fact]
  public void Authorize_RefusesAnEditorWithTheWrongTokenInEditMode()
  {
    var backend = new TcpBackend { EditMode = true, ExpectedToken = "secret" };

    Assert.False(backend.Authorize(Handshake(RoleEditor, "wrong")));
  }

  [Fact]
  public void Authorize_RefusesAnEmptyPayload()
  {
    // The role byte itself is missing - Authorize refuses before it can even read one. Not the same
    // check as the handshake-type-byte check in HandleClient/ServerReceiveLoop, which runs before
    // Authorize is ever called; this is the truncated-payload case Authorize itself guards.
    var backend = new TcpBackend { EditMode = true, ExpectedToken = "secret" };

    Assert.False(backend.Authorize(Array.Empty<byte>()));
  }

  [Fact]
  public void Authorize_AcceptsAnUnrecognizedRoleByte()
  {
    // Only role == RoleEditor triggers the token check; anything else - including a role the transport
    // does not otherwise recognize - falls through to the same acceptance a player gets.
    var backend = new TcpBackend { EditMode = true, ExpectedToken = "secret" };

    Assert.True(backend.Authorize(Handshake(RoleUnknown)));
  }
}
