using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace ECS3DNetTransport;

// Raw-TCP backend. Each message is framed on the wire as a 4-byte big-endian length (1 type byte +
// payload) followed by the bytes. Inbound delivery is push: a per-connection thread reads frames and
// invokes the C++ callback (via Transport.DeliverServer/DeliverClient) on the socket thread. Outbound is
// a direct call from the tick thread.
internal sealed class TcpBackend : TransportBackend
{
  // -- Server --
  private TcpListener? _listener;
  private Thread? _acceptThread;
  private volatile bool _serverRunning;

  private readonly List<Connection> _clients = new();
  // Accepted sockets that have not yet cleared the handshake. Tracked separately from _clients so an
  // unauthorized peer is never on the broadcast list, and so ServerStop still closes it.
  private readonly List<TcpClient> _pending = new();
  private readonly object _clientsLock = new();

  // A socket that cleared the handshake, paired with the id C++ knows it by so a send that fails on the
  // broadcast path can name the connection it dropped. A class rather than a record on purpose: the
  // broadcast snapshot and the receive loop both drop a connection with List.Remove, which must match by
  // reference identity so it removes that one connection and nothing that merely looks like it.
  private sealed class Connection(TcpClient client, int connId)
  {
    public readonly TcpClient Client = client;
    public readonly int ConnId = connId;
  }

  // A stable, monotonically-increasing id handed to each accepted connection, surfaced to C++ on every
  // inbound message so the server can keep per-client state (e.g. input). Never reused within a run.
  private int _nextConnId;

  // Bounds how long an accepted socket may sit unauthorized before it is dropped. Without this, a peer
  // that opens a connection and sends nothing holds a thread and a socket - and, before this change,
  // a spot on the broadcast list - forever.
  private const int HandshakeTimeoutMs = 5000;

  // What one whole broadcast may spend blocking on peers that have stopped draining their sockets, not
  // what each peer may spend: the budget is shared across the fan-out, so ten stalled peers cost this
  // once rather than ten times over. Broadcasts run on the tick thread, so the bound is what physics and
  // scripts can lose to the network in a tick. Only the connection whose own send ran out of budget is
  // dropped; peers the broadcast never reached are skipped for that message and kept. Two seconds because
  // a stall then costs at most one tick's worth before the peer responsible is dropped, while still being
  // far longer than any plausible snapshot send takes on a healthy link.
  private const int SendTimeoutMs = 2000;

  // -- Client --
  private TcpClient? _client;
  private Thread? _clientThread;
  private volatile bool _clientRunning;

  // Comfortably under the callers' 15 s retry budget so several attempts fit, and long enough for a real
  // WAN handshake. Unbounded, one attempt against a host that routes but never answers runs to the OS
  // connect timeout (~21 s on Windows) and outlives the whole budget on its own.
  private const int ConnectTimeoutMs = 3000;

  public override void ServerStart(int port, bool editMode, string expectedToken)
  {
    if (_serverRunning)
    {
      return;
    }

    EditMode = editMode;
    ExpectedToken = expectedToken;

    _listener = new TcpListener(IPAddress.Any, port);
    _listener.Start();
    _serverRunning = true;

    _acceptThread = new Thread(AcceptLoop) { IsBackground = true, Name = "ecs3d-net-accept" };
    _acceptThread.Start();

    Transport.Log(TransportLogLevel.Info, $"TCP server listening on port {port} (editMode={EditMode}).");
  }

  public override void ServerStop()
  {
    _serverRunning = false;

    try { _listener?.Stop(); } catch { /* already closed */ }
    _listener = null;

    lock (_clientsLock)
    {
      foreach (var conn in _clients)
      {
        try { conn.Client.Close(); } catch { /* ignore */ }
      }

      _clients.Clear();

      foreach (var client in _pending)
      {
        try { client.Close(); } catch { /* ignore */ }
      }

      _pending.Clear();
    }
  }

  public override int ServerConnectionCount()
  {
    lock (_clientsLock)
    {
      return _clients.Count;
    }
  }

  public override void ServerBroadcast(byte type, nint data, int len)
  {
    if (TooLargeToSend(len))
    {
      return;
    }

    var frame = Frame(type, data, len);

    // The fan-out runs off a snapshot so the sends happen outside _clientsLock. This is the tick thread,
    // and a Write to a peer that has stopped reading blocks until the budget below runs out; holding the
    // lock across that would block the accept loop and every receive loop's cleanup along with it.
    Connection[] clients;
    lock (_clientsLock)
    {
      clients = _clients.ToArray();
    }

    // One budget for the whole fan-out. A per-connection timeout would still let K stalled peers hold the
    // tick thread for K times the timeout in a single broadcast, which is the stall this avoids.
    var deadline = Environment.TickCount64 + SendTimeoutMs;
    var skipped = 0;

    foreach (var conn in clients)
    {
      var remaining = deadline - Environment.TickCount64;
      if (remaining <= 0)
      {
        // An earlier peer spent the budget. Skip the rest of this broadcast rather than dropping them:
        // they have done nothing wrong, and reaping them would punish healthy peers for their position in
        // the list. They miss this one message and are sent the next broadcast as usual.
        ++skipped;
        continue;
      }

      var sent = false;
      try
      {
        // Never zero: SocketOptionName.SendTimeout reads 0 as "no timeout", which is what this removes.
        conn.Client.SendTimeout = (int)Math.Max(remaining, 1);
        conn.Client.GetStream().Write(frame, 0, frame.Length);
        sent = true;
      }
      catch
      {
        // The connection dropped mid-send, or did not accept the frame inside the remaining budget.
      }

      if (!sent && Reap(conn))
      {
        Transport.Log(TransportLogLevel.Warn,
          $"Dropping connection {conn.ConnId}: the send failed or timed out.");
      }
    }

    // One line per broadcast rather than one per connection, so a peer stalling tick after tick is visible
    // in the log without burying it.
    if (skipped > 0)
    {
      Transport.Log(TransportLogLevel.Warn,
        $"Skipped {skipped} connection(s): this broadcast spent its whole {SendTimeoutMs} ms budget.");
    }
  }

  // Closes a connection and takes it off the broadcast list. True only when this call is the one that
  // removed it: a peer that closed itself is reaped by its own receive loop, and that is an ordinary
  // disconnect rather than something to warn about.
  //
  // Closing the socket makes the connection's blocked read throw, and ServerReceiveLoop's exit path owns
  // the DeliverServerDisconnect for it - delivering one here too would free the player slot twice. Closing
  // an already-closed socket is harmless.
  private bool Reap(Connection conn)
  {
    try { conn.Client.Close(); } catch { /* ignore */ }

    lock (_clientsLock)
    {
      // By reference rather than by index: the list may have changed since the broadcast's snapshot.
      return _clients.Remove(conn);
    }
  }

  private void AcceptLoop()
  {
    while (_serverRunning)
    {
      TcpClient client;
      try
      {
        client = _listener!.AcceptTcpClient();
      }
      catch
      {
        break;
      }

      client.NoDelay = true;

      // A starting value only; each broadcast narrows this to whatever is left of its own budget. It
      // matters because a socket defaults to no send timeout at all, and this one is set before any
      // broadcast can reach the connection.
      client.SendTimeout = SendTimeoutMs;

      var connId = Interlocked.Increment(ref _nextConnId);

      // Not added to _clients yet - it has not proven itself with a handshake, and _clients is the
      // broadcast list. Tracked in _pending instead so ServerStop can still close it.
      //
      // ServerStop can run concurrently with this: it may have already taken _clientsLock, closed and
      // cleared _pending, and returned by the time a connection that was queued by the OS just before
      // _listener.Stop() reaches here. Re-checking _serverRunning inside the lock closes that window -
      // if the server already stopped, the socket is closed here instead of being added to a list
      // nothing will ever look at again, and its receive thread is never started (so it never reaches
      // DeliverServerDisconnect either).
      bool accepted;
      lock (_clientsLock)
      {
        accepted = _serverRunning;
        if (accepted)
        {
          _pending.Add(client);
        }
      }

      if (!accepted)
      {
        try { client.Close(); } catch { /* ignore */ }
        continue;
      }

      var thread = new Thread(() => ServerReceiveLoop(client, connId))
      {
        IsBackground = true,
        Name = "ecs3d-net-client"
      };
      thread.Start();
    }
  }

  private void ServerReceiveLoop(TcpClient client, int connId)
  {
    Connection? conn = null;

    try
    {
      var stream = client.GetStream();

      // The first frame must be the handshake. Authorize from it (e.g. reject an editor against a
      // play-only server, or one with a bad token) before delivering any protocol message to C++, and
      // before this socket is added to the broadcast list. Bounded two ways: ReceiveTimeout unblocks a
      // syscall against a peer that sends nothing at all, and the deadline below bounds the whole
      // handshake against a peer that trickles bytes just fast enough to keep individual reads from
      // timing out (ReadExact loops per partial read, so a per-read timeout alone doesn't bound the total).
      client.ReceiveTimeout = HandshakeTimeoutMs;
      var handshakeDeadline = Environment.TickCount64 + HandshakeTimeoutMs;
      if (ReadFrame(stream, out var handshakeType, out var handshakePayload, MaxHandshakeBytes, handshakeDeadline) &&
          handshakeType == HandshakeType && Authorize(handshakePayload))
      {
        // Authorize succeeded: tell the native side which role this connection was actually granted, so
        // it can enforce that role on every later message rather than trusting one the sender claims.
        Transport.DeliverServerAuthorized(connId, handshakePayload[0]);

        client.ReceiveTimeout = 0;

        conn = new Connection(client, connId);

        lock (_clientsLock)
        {
          _pending.Remove(client);
          _clients.Add(conn);
        }

        while (_serverRunning)
        {
          if (!ReadFrame(stream, out var type, out var payload))
          {
            break;
          }

          Transport.DeliverServer(connId, type, payload);
        }
      }
      else
      {
        Transport.Log(TransportLogLevel.Warn, "Rejected a connection that failed the handshake.");
      }
    }
    catch
    {
      // fall through to cleanup
    }

    lock (_clientsLock)
    {
      if (conn != null)
      {
        _clients.Remove(conn);
      }

      _pending.Remove(client);
    }

    try { client.Close(); } catch { /* ignore */ }

    // Let the server release any player slot bound to this connection. A no-op on the C++ side if the
    // connection never joined (e.g. a rejected handshake), since it holds no slot for it.
    Transport.DeliverServerDisconnect(connId);
  }

  public override byte ClientConnect(string host, int port, byte role, string token)
  {
    if (_clientRunning)
    {
      return 1;
    }

    try
    {
      _client = new TcpClient();

      // Scoped to the connect alone: disposing the source kills its pending timer, so a slow but
      // successful connect cannot be aborted once the connection is live.
      using (var connectCts = new CancellationTokenSource(ConnectTimeoutMs))
      {
        _client.ConnectAsync(host, port, connectCts.Token).AsTask().GetAwaiter().GetResult();
      }

      _client.NoDelay = true;

      // Send role + token as the first frame so the server can authorize this connection (in
      // particular grant Role.editor) before any protocol message. Same wire format everywhere.
      SendHandshake(role, token);
    }
    catch (OperationCanceledException)
    {
      Transport.Log(TransportLogLevel.Warn, $"Connect to {host}:{port} timed out after {ConnectTimeoutMs} ms.");

      try { _client?.Close(); } catch { /* ignore */ }
      _client = null;

      return 0;
    }
    catch (Exception e)
    {
      Transport.Log(TransportLogLevel.Warn, $"Client failed to connect to {host}:{port}: {e.Message}");

      try { _client?.Close(); } catch { /* ignore */ }
      _client = null;

      return 0;
    }

    _clientRunning = true;

    _clientThread = new Thread(() => ClientReceiveLoop(_client!)) { IsBackground = true, Name = "ecs3d-net-recv" };
    _clientThread.Start();

    Transport.Log(TransportLogLevel.Info, $"Connected to {host}:{port}.");
    return 1;
  }

  public override void ClientDisconnect()
  {
    DisconnectClient();
  }

  private void DisconnectClient()
  {
    _clientRunning = false;

    // Take whatever is currently there so a concurrent ClientReceiveLoop cleanup cannot also see it and
    // double-close it.
    var client = Interlocked.Exchange(ref _client, null);
    try { client?.Close(); } catch { /* already closed */ }
  }

  public override void ClientSend(byte type, nint data, int len)
  {
    var client = _client;
    if (client is null || TooLargeToSend(len))
    {
      return;
    }

    var frame = Frame(type, data, len);
    try
    {
      // GetStream() is included in the try: a concurrent teardown (ClientDisconnect or the receive loop)
      // may dispose this same client between the read above and here, and a disposed TcpClient throws
      // ObjectDisposedException out of GetStream() itself, before Write ever runs.
      client.GetStream().Write(frame, 0, frame.Length);
    }
    catch
    {
      // The write failed on this specific connection. Drop that same instance rather than whatever is
      // current: a reconnect may already have installed a different, healthy one by the time this runs.
      if (Interlocked.CompareExchange(ref _client, null, client) == client)
      {
        try { client.Close(); } catch { /* already closed */ }
      }
    }
  }

  private void ClientReceiveLoop(TcpClient client)
  {
    try
    {
      var stream = client.GetStream();
      while (_clientRunning)
      {
        if (!ReadFrame(stream, out var type, out var payload))
        {
          break;
        }

        Transport.DeliverClient(type, payload);
      }
    }
    catch
    {
      // fall through
    }

    _clientRunning = false;

    // Cleared first, and only if it still holds the instance this loop owns - a concurrent ClientConnect
    // may already have installed a newer connection, which this loop must leave alone - so ClientSend
    // never reads a reference through the field once the close below has started. The socket itself is
    // then closed unconditionally: Close is idempotent, so this is harmless even if ClientDisconnect
    // already closed the same instance.
    Interlocked.CompareExchange(ref _client, null, client);
    try { client.Close(); } catch { /* already closed */ }

    // The single delivery point for a lost connection, whether the peer closed it, a read failed, or
    // ClientDisconnect closed our own socket to make this loop exit - the native side tells the two
    // apart via m_disconnectRequested and no-ops the latter.
    Transport.DeliverClientDisconnect();
  }

  // -- Framing helpers --

  private void SendHandshake(byte role, string token)
  {
    var tokenBytes = Encoding.UTF8.GetBytes(token);

    var payload = new byte[1 + tokenBytes.Length];
    payload[0] = role;
    Array.Copy(tokenBytes, 0, payload, 1, tokenBytes.Length);

    var frame = FrameBytes(HandshakeType, payload);
    _client!.GetStream().Write(frame, 0, frame.Length);
  }

  private static byte[] FrameBytes(byte type, byte[] payload)
  {
    var frame = new byte[4 + 1 + payload.Length];
    BinaryPrimitives.WriteInt32BigEndian(frame, 1 + payload.Length);
    frame[4] = type;
    Array.Copy(payload, 0, frame, 5, payload.Length);

    return frame;
  }

  private static byte[] Frame(byte type, nint data, int len)
  {
    var frame = new byte[4 + 1 + len];
    BinaryPrimitives.WriteInt32BigEndian(frame, 1 + len);
    frame[4] = type;
    if (len > 0)
    {
      Marshal.Copy(data, frame, 5, len);
    }

    return frame;
  }

  // deadline is an Environment.TickCount64 value the whole frame must be read by, or null for no outer
  // bound - every caller but the handshake read leaves it null, since the steady-state message loop must
  // stay blocking and untimed.
  private static bool ReadFrame(Stream stream, out byte type, out byte[] payload, int maxBytes = MaxMessageBytes,
    long? deadline = null)
  {
    type = 0;
    payload = Array.Empty<byte>();

    Span<byte> header = stackalloc byte[4];
    if (!ReadExact(stream, header, deadline))
    {
      return false;
    }

    // The length is the peer's word, and the handshake is itself a frame - so this runs for anything that
    // can open a socket, before Authorize has seen a byte. Unbounded, a length just under int.MaxValue
    // asks for a two gigabyte allocation, and the copy below holds it twice over at once. The handshake
    // read passes a far smaller ceiling than the rest, since it carries a role byte and a token.
    var bodyLen = BinaryPrimitives.ReadInt32BigEndian(header);
    if (bodyLen < 1)
    {
      return false;
    }

    if (bodyLen > maxBytes)
    {
      // Logged, because a refusal and a closed socket are the same false to the caller. A message this
      // size is either an attack or a peer that has outgrown the limit, and both are worth seeing.
      Transport.Log(TransportLogLevel.Warn, $"Refused a {bodyLen} byte frame; the limit here is {maxBytes}.");

      return false;
    }

    var body = new byte[bodyLen];
    if (!ReadExact(stream, body, deadline))
    {
      return false;
    }

    type = body[0];
    payload = new byte[bodyLen - 1];
    Array.Copy(body, 1, payload, 0, bodyLen - 1);
    return true;
  }

  private static bool ReadExact(Stream stream, Span<byte> buffer, long? deadline = null)
  {
    var read = 0;
    while (read < buffer.Length)
    {
      // Checked before each syscall rather than relying on ReceiveTimeout alone: ReceiveTimeout bounds
      // one Read call, but a partial frame keeps this loop calling Read again, so a peer trickling one
      // byte in just under the timeout each time would otherwise never trip it.
      if (deadline.HasValue && Environment.TickCount64 >= deadline.Value)
      {
        return false;
      }

      var n = stream.Read(buffer.Slice(read));
      if (n <= 0)
      {
        return false;
      }

      read += n;
    }

    return true;
  }
}
