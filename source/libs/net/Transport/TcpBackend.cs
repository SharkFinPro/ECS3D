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

  private readonly List<TcpClient> _clients = new();
  // Accepted sockets that have not yet cleared the handshake. Tracked separately from _clients so an
  // unauthorized peer is never on the broadcast list, and so ServerStop still closes it.
  private readonly List<TcpClient> _pending = new();
  private readonly object _clientsLock = new();

  // A stable, monotonically-increasing id handed to each accepted connection, surfaced to C++ on every
  // inbound message so the server can keep per-client state (e.g. input). Never reused within a run.
  private int _nextConnId;

  // Bounds how long an accepted socket may sit unauthorized before it is dropped. Without this, a peer
  // that opens a connection and sends nothing holds a thread and a socket - and, before this change,
  // a spot on the broadcast list - forever.
  private const int HandshakeTimeoutMs = 5000;

  // -- Client --
  private TcpClient? _client;
  private Thread? _clientThread;
  private volatile bool _clientRunning;

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

    Console.WriteLine($"[Transport] TCP server listening on port {port} (editMode={EditMode}).");
  }

  public override void ServerStop()
  {
    _serverRunning = false;

    try { _listener?.Stop(); } catch { /* already closed */ }
    _listener = null;

    lock (_clientsLock)
    {
      foreach (var client in _clients)
      {
        try { client.Close(); } catch { /* ignore */ }
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

    lock (_clientsLock)
    {
      for (var i = _clients.Count - 1; i >= 0; --i)
      {
        try
        {
          _clients[i].GetStream().Write(frame, 0, frame.Length);
        }
        catch
        {
          // The connection dropped mid-send; reap it.
          try { _clients[i].Close(); } catch { /* ignore */ }
          _clients.RemoveAt(i);
        }
      }
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
        client.ReceiveTimeout = 0;

        lock (_clientsLock)
        {
          _pending.Remove(client);
          _clients.Add(client);
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
        Console.Error.WriteLine("[Transport] Rejected a connection that failed the handshake.");
      }
    }
    catch
    {
      // fall through to cleanup
    }

    lock (_clientsLock)
    {
      _clients.Remove(client);
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
      _client.Connect(host, port);
      _client.NoDelay = true;

      // Send role + token as the first frame so the server can authorize this connection (in
      // particular grant Role.editor) before any protocol message. Same wire format everywhere.
      SendHandshake(role, token);
    }
    catch (Exception e)
    {
      Console.Error.WriteLine($"[Transport] Client failed to connect to {host}:{port}: {e.Message}");
      _client = null;
      return 0;
    }

    _clientRunning = true;

    _clientThread = new Thread(ClientReceiveLoop) { IsBackground = true, Name = "ecs3d-net-recv" };
    _clientThread.Start();

    Console.WriteLine($"[Transport] Connected to {host}:{port}.");
    return 1;
  }

  public override void ClientDisconnect()
  {
    DisconnectClient();
  }

  private void DisconnectClient()
  {
    _clientRunning = false;

    try { _client?.Close(); } catch { /* already closed */ }
    _client = null;
  }

  public override void ClientSend(byte type, nint data, int len)
  {
    var stream = _client?.GetStream();
    if (stream is null || TooLargeToSend(len))
    {
      return;
    }

    var frame = Frame(type, data, len);
    try
    {
      stream.Write(frame, 0, frame.Length);
    }
    catch
    {
      DisconnectClient();
    }
  }

  private void ClientReceiveLoop()
  {
    try
    {
      var stream = _client!.GetStream();
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
      Console.Error.WriteLine($"[Transport] Refused a {bodyLen} byte frame; the limit here is {maxBytes}.");

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
