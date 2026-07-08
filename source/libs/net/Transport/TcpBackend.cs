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
  private readonly object _clientsLock = new();

  // A stable, monotonically-increasing id handed to each accepted connection, surfaced to C++ on every
  // inbound message so the server can keep per-client state (e.g. input). Never reused within a run.
  private int _nextConnId;

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

      lock (_clientsLock)
      {
        _clients.Add(client);
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
      // play-only server, or one with a bad token) before delivering any protocol message to C++.
      if (ReadFrame(stream, out var handshakeType, out var handshakePayload) &&
          handshakeType == HandshakeType && Authorize(handshakePayload))
      {
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
    if (stream is null)
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

  private static bool ReadFrame(Stream stream, out byte type, out byte[] payload)
  {
    type = 0;
    payload = Array.Empty<byte>();

    Span<byte> header = stackalloc byte[4];
    if (!ReadExact(stream, header))
    {
      return false;
    }

    var bodyLen = BinaryPrimitives.ReadInt32BigEndian(header);
    if (bodyLen < 1)
    {
      return false;
    }

    var body = new byte[bodyLen];
    if (!ReadExact(stream, body))
    {
      return false;
    }

    type = body[0];
    payload = new byte[bodyLen - 1];
    Array.Copy(body, 1, payload, 0, bodyLen - 1);
    return true;
  }

  private static bool ReadExact(Stream stream, Span<byte> buffer)
  {
    var read = 0;
    while (read < buffer.Length)
    {
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
