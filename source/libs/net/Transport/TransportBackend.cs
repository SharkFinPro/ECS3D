using System.Text;

namespace ECS3DNetTransport;

// The wire transports ECS3DNet can speak. Selected (hardcoded) by Transport.Protocol.
public enum TransportProtocol
{
  Tcp,
  WebSocket,
  // Udp, -- coming later
}

// A backend is one self-contained implementation of the socket layer for a single wire protocol. It owns
// its own listener/connection state and socket threads; Transport just forwards the native exports to the
// selected instance. Inbound messages are pushed up to C++ via Transport.DeliverServer/DeliverClient.
//
// Everything below the handshake is transport-specific and lives in the concrete subclasses (TcpBackend,
// WebSocketBackend). What's shared — and must stay identical across transports — is the connection
// handshake/authorization policy, which lives here.
internal abstract class TransportBackend
{
  // The connection handshake is the first message on every connection: a reserved type byte (outside the
  // C++ MessageType enum, so it's consumed here and never delivered to C++) carrying [role byte][token
  // UTF-8]. The server authorizes the connection from it before any protocol message flows.
  protected const byte HandshakeType = 0xFF;
  protected const byte RolePlayer = 0;
  protected const byte RoleEditor = 1;

  // editMode is the launch-capability gate: only an edit-mode server may grant Role.editor at the
  // handshake, and only when the presented token matches expectedToken (see Authorize). Set at ServerStart.
  protected bool EditMode;
  protected string ExpectedToken = "";

  public abstract void ServerStart(int port, bool editMode, string expectedToken);
  public abstract void ServerStop();
  public abstract int ServerConnectionCount();
  public abstract void ServerBroadcast(byte type, nint data, int len);

  public abstract byte ClientConnect(string host, int port, byte role, string token);
  public abstract void ClientDisconnect();
  public abstract void ClientSend(byte type, nint data, int len);

  // Decides whether a connection presenting this handshake payload ([role byte][token UTF-8]) is
  // allowed onto the server at all. Players connect freely. An editor is admitted too — even against a
  // non-edit server, where it gets a read-only view (the server simply honors no edits from it). The one
  // hard rejection is a real auth failure: an editor offering the wrong token to an edit server that
  // configured one. Whether an admitted editor may actually edit is conveyed separately via editStatus.
  protected bool Authorize(byte[] payload)
  {
    if (payload.Length < 1)
    {
      return false;
    }

    var role = payload[0];
    var token = payload.Length > 1 ? Encoding.UTF8.GetString(payload, 1, payload.Length - 1) : "";

    if (role == RoleEditor && EditMode && ExpectedToken.Length != 0 && token != ExpectedToken)
    {
      return false;
    }

    return true;
  }
}
