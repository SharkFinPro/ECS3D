using System;
using System.Text;
using System.Threading;

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
// WebSocketBackend). What's shared - and must stay identical across transports - is the connection
// handshake/authorization policy, which lives here.
internal abstract class TransportBackend
{
  // The connection handshake is the first message on every connection: a reserved type byte (outside the
  // C++ MessageType enum, so it's consumed here and never delivered to C++) carrying [role byte][token
  // UTF-8]. The server authorizes the connection from it before any protocol message flows.
  protected const byte HandshakeType = 0xFF;
  protected const byte RolePlayer = 0;
  protected const byte RoleEditor = 1;

  // The most a single inbound message may be before the connection is dropped. A peer declares its own
  // frame length, so without a ceiling a peer that has not even handshaked yet can name any number and
  // have the server allocate it - four bytes of input for gigabytes of memory, repeatable per connection.
  //
  // Generous rather than tight: the largest legitimate message is a full project snapshot, which carries
  // every scene, prefab body and asset record, and the point is to refuse the absurd rather than to
  // predict the biggest real project. Shared here so both transports refuse the same thing.
  //
  // Note this bounds the declared length, not the memory: a message at the ceiling is held two or three
  // times over while it is assembled and copied out, so budget a multiple of this rather than this.
  protected const int MaxMessageBytes = 64 * 1024 * 1024;

  // The handshake is [role byte][token], so it has no business being large - and it is the one frame
  // read before the peer has been authorized at all. Capping it separately means the pre-auth surface is
  // a few kilobytes rather than the whole ceiling above, which everything after the handshake gets.
  protected const int MaxHandshakeBytes = 4 * 1024;

  // An outbound message over the ceiling would be refused by every peer that received it, and refused
  // silently - the receiving loop cannot tell an over-long frame from a closed socket. Caught at the
  // sender instead, where there is something useful to say about it.
  protected static bool TooLargeToSend(int len)
  {
    if (1 + len <= MaxMessageBytes)
    {
      return false;
    }

    Transport.Log(TransportLogLevel.Error, $"Refusing to send a {1 + len} byte message; the limit is {MaxMessageBytes}.");

    return true;
  }

  // editMode is the launch-capability gate: only an edit-mode server may grant Role.editor at the
  // handshake, and only when the presented token matches expectedToken (see Authorize). Set at ServerStart.
  // Internal (rather than protected) so ECS3DManagedTests can drive Authorize directly against a concrete
  // backend instance via InternalsVisibleTo (see Transport/AssemblyInfo.cs).
  internal bool EditMode;
  internal string ExpectedToken = "";

  public abstract void ServerStart(int port, bool editMode, string expectedToken);
  public abstract void ServerStop();
  public abstract int ServerConnectionCount();
  public abstract void ServerBroadcast(byte type, nint data, int len);

  // Sends to exactly the connIdCount connections named by connIds (a native int32 array), rather than
  // every connection like ServerBroadcast - for data (the server's own log) that only authorized editor
  // connections may see. One call for the whole fan-out: implementations apply the same shared time
  // budget across it that ServerBroadcast applies across every connection, so a stalled connection in the
  // list cannot cost the caller more than that one shared budget no matter how many are named. A connId
  // that has since disconnected is silently skipped.
  public abstract void ServerSendToMany(nint connIds, int connIdCount, byte type, nint data, int len);

  public abstract byte ClientConnect(string host, int port, byte role, string token);
  public abstract void ClientDisconnect();
  public abstract void ClientSend(byte type, nint data, int len);

  // Decides whether a connection presenting this handshake payload ([role byte][token UTF-8]) is
  // allowed onto the server at all. Players connect freely. An editor is admitted too - even against a
  // non-edit server, where it gets a read-only view (the server simply honors no edits from it). The one
  // hard rejection is a real auth failure: an editor offering the wrong token to an edit server that
  // configured one. Whether an admitted editor may actually edit is conveyed separately via editStatus.
  // Internal (rather than protected) so ECS3DManagedTests can call it directly against a concrete backend
  // instance via InternalsVisibleTo (see Transport/AssemblyInfo.cs).
  internal bool Authorize(byte[] payload)
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

  // How long ServerStop/DisconnectClient wait for a socket thread to exit after its socket is closed.
  // Bounded so a stuck thread (e.g. one wedged in native socket teardown) cannot hang process shutdown;
  // logged as a warning when it fires, since it should not happen in the normal case. Shared by both
  // backends' shutdown paths.
  protected const int ShutdownJoinTimeoutMs = 3000;

  // Waits up to ShutdownJoinTimeoutMs for thread to exit, logging a warning if it takes longer than that
  // instead of waiting past it. A no-op for null, an already-finished thread, or the calling thread
  // itself - joining the current thread would deadlock, and ServerStop/DisconnectClient could in
  // principle be reached from a callback running on one of these threads.
  protected static void JoinThread(Thread? thread, string label)
  {
    if (thread == null || thread == Thread.CurrentThread || !thread.IsAlive)
    {
      return;
    }

    if (!thread.Join(ShutdownJoinTimeoutMs))
    {
      Transport.Log(TransportLogLevel.Warn,
        $"Timed out after {ShutdownJoinTimeoutMs} ms waiting for the {label} thread to exit during shutdown.");
    }
  }
}
