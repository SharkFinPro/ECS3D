using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading;

namespace ECS3DNetTransport;

// WebSocket backend. Each protocol message travels as a single binary WebSocket message whose first byte
// is the type and whose remaining bytes are the payload; WebSocket delimits messages for us, so there is
// no length prefix on the wire.
//
// The server speaks WebSocket over a raw TCP accept: it performs the RFC 6455 upgrade handshake by hand
// (so it can bind IPAddress.Any without an HttpListener URL reservation) and then wraps the stream with
// WebSocket.CreateFromStream. The client uses ClientWebSocket against ws://host:port/. Inbound delivery is
// push (Transport.DeliverServer/DeliverClient); outbound is a direct call from the tick thread.
internal sealed class WebSocketBackend : TransportBackend
{
  // RFC 6455 magic GUID, concatenated with the client's Sec-WebSocket-Key to compute the accept token.
  private const string WebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  private static readonly TimeSpan KeepAlive = TimeSpan.FromSeconds(30);

  // Why these two settings exist: unlike the raw-TCP backend (which writes whole frames with a blocking
  // Stream.Write that never touches the thread pool), the WebSocket API is async-only, so every send goes
  // through SendAsync().GetResult(). A send that fits the socket's send buffer completes synchronously and
  // returns inline; one that doesn't falls back to the async path, whose continuation needs a thread-pool
  // thread. Because our socket threads block on .GetResult(), the pool has to *inject* that thread, which
  // it throttles to ~one per 15.6ms — a per-message stall that made WebSocket feel laggy versus TCP.
  //
  //  - A send buffer larger than a typical message keeps the common case on the synchronous path.
  //  - Raising the thread-pool minimum removes the injection delay for the rare oversized message that
  //    still goes async, so it degrades to plain bandwidth cost (same as TCP) instead of a 15ms hitch.
  private const int SocketBufferSize = 1 << 20; // 1 MiB per direction.

  static WebSocketBackend()
  {
    ThreadPool.GetMinThreads(out var worker, out var io);
    ThreadPool.SetMinThreads(Math.Max(worker, 64), Math.Max(io, 64));
  }

  // -- Server --
  private TcpListener? _listener;
  private Thread? _acceptThread;
  private volatile bool _serverRunning;

  private readonly List<Connection> _connections = new();
  private readonly object _clientsLock = new();

  // A stable, monotonically-increasing id handed to each accepted connection, surfaced to C++ on every
  // inbound message so the server can keep per-client state (e.g. input). Never reused within a run.
  private int _nextConnId;

  // -- Client --
  private WebSocket? _client;
  private HttpMessageInvoker? _clientInvoker;
  private CancellationTokenSource? _clientCts;
  private readonly SemaphoreSlim _clientSendLock = new(1, 1);
  private Thread? _clientThread;
  private volatile bool _clientRunning;

  // A WebSocket is safe for one concurrent send and one concurrent receive, but not for concurrent sends.
  // The send lock serializes broadcasts (and any future sender) onto a single connection.
  private sealed class Connection(WebSocket socket, CancellationTokenSource cts)
  {
    public readonly WebSocket Socket = socket;
    public readonly CancellationTokenSource Cts = cts;
    public readonly SemaphoreSlim SendLock = new(1, 1);
  }

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

    Console.WriteLine($"[Transport] WebSocket server listening on port {port} (editMode={EditMode}).");
  }

  public override void ServerStop()
  {
    _serverRunning = false;

    try { _listener?.Stop(); } catch { /* already closed */ }
    _listener = null;

    lock (_clientsLock)
    {
      foreach (var conn in _connections)
      {
        Close(conn.Socket, conn.Cts);
      }

      _connections.Clear();
    }
  }

  public override int ServerConnectionCount()
  {
    lock (_clientsLock)
    {
      return _connections.Count;
    }
  }

  public override void ServerBroadcast(byte type, nint data, int len)
  {
    var message = BuildMessage(type, data, len);

    lock (_clientsLock)
    {
      for (var i = _connections.Count - 1; i >= 0; --i)
      {
        if (!SendMessage(_connections[i], message))
        {
          // The connection dropped mid-send; reap it.
          Close(_connections[i].Socket, _connections[i].Cts);
          _connections.RemoveAt(i);
        }
      }
    }
  }

  private void AcceptLoop()
  {
    while (_serverRunning)
    {
      TcpClient tcp;
      try
      {
        tcp = _listener!.AcceptTcpClient();
      }
      catch
      {
        break;
      }

      var thread = new Thread(() => HandleClient(tcp))
      {
        IsBackground = true,
        Name = "ecs3d-net-client"
      };
      thread.Start();
    }
  }

  private void HandleClient(TcpClient tcp)
  {
    WebSocket? ws = null;
    CancellationTokenSource? cts = null;
    Connection? conn = null;

    try
    {
      tcp.NoDelay = true;
      tcp.SendBufferSize = SocketBufferSize;
      tcp.ReceiveBufferSize = SocketBufferSize;
      var stream = tcp.GetStream();

      // Upgrade the raw TCP connection to WebSocket, then let the BCL handle framing from here on.
      if (!PerformServerHandshake(stream))
      {
        return;
      }

      ws = WebSocket.CreateFromStream(stream, isServer: true, subProtocol: null, keepAliveInterval: KeepAlive);
      cts = new CancellationTokenSource();

      // The first message must be the connection handshake. Authorize from it (e.g. reject an editor
      // against a play-only server, or one with a bad token) before delivering any protocol message.
      var first = ReceiveMessage(ws, cts.Token);
      if (first is null || first.Length < 1 || first[0] != HandshakeType || !Authorize(Payload(first)))
      {
        Console.Error.WriteLine("[Transport] Rejected a connection that failed the handshake.");
        return;
      }

      var connId = Interlocked.Increment(ref _nextConnId);

      conn = new Connection(ws, cts);
      lock (_clientsLock)
      {
        _connections.Add(conn);
      }

      while (_serverRunning)
      {
        var message = ReceiveMessage(ws, cts.Token);
        if (message is null || message.Length < 1)
        {
          break;
        }

        Transport.DeliverServer(connId, message[0], Payload(message));
      }
    }
    catch
    {
      // fall through to cleanup
    }
    finally
    {
      if (conn != null)
      {
        lock (_clientsLock)
        {
          _connections.Remove(conn);
        }
      }

      if (ws != null)
      {
        Close(ws, cts);
      }
      else
      {
        cts?.Dispose();
      }

      try { tcp.Close(); } catch { /* ignore */ }
    }
  }

  public override byte ClientConnect(string host, int port, byte role, string token)
  {
    if (_clientRunning)
    {
      return 1;
    }

    try
    {
      var ws = new ClientWebSocket();
      ws.Options.KeepAliveInterval = KeepAlive;

      // ClientWebSocket gives no way to set NoDelay on its socket, so it would otherwise leave Nagle's
      // algorithm enabled — small per-tick messages get held ~40ms (Nagle + delayed ACK), the lag the
      // TCP backend avoids by setting NoDelay on both ends. A ConnectCallback lets us own the socket and
      // disable Nagle ourselves.
      var invoker = new HttpMessageInvoker(new SocketsHttpHandler
      {
        ConnectCallback = static async (context, ct) =>
        {
          var socket = new Socket(SocketType.Stream, ProtocolType.Tcp)
          {
            NoDelay = true,
            SendBufferSize = SocketBufferSize,
            ReceiveBufferSize = SocketBufferSize,
          };
          try
          {
            await socket.ConnectAsync(context.DnsEndPoint, ct).ConfigureAwait(false);
            return new NetworkStream(socket, ownsSocket: true);
          }
          catch
          {
            socket.Dispose();
            throw;
          }
        }
      });

      ws.ConnectAsync(new Uri($"ws://{host}:{port}/"), invoker, CancellationToken.None).GetAwaiter().GetResult();

      _client = ws;
      _clientInvoker = invoker;
      _clientCts = new CancellationTokenSource();

      // Send role + token as the first message so the server can authorize this connection (in
      // particular grant Role.editor) before any protocol message. Same wire format everywhere.
      SendHandshake(role, token);
    }
    catch (Exception e)
    {
      Console.Error.WriteLine($"[Transport] Client failed to connect to {host}:{port}: {e.Message}");
      DisconnectClient();
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

    var ws = _client;
    var cts = _clientCts;
    var invoker = _clientInvoker;
    _client = null;
    _clientCts = null;
    _clientInvoker = null;

    if (ws != null)
    {
      Close(ws, cts);
    }
    else
    {
      cts?.Dispose();
    }

    invoker?.Dispose();
  }

  public override void ClientSend(byte type, nint data, int len)
  {
    var ws = _client;
    var cts = _clientCts;
    if (ws is null || cts is null)
    {
      return;
    }

    var message = BuildMessage(type, data, len);
    if (!SendRaw(ws, _clientSendLock, message, cts.Token))
    {
      DisconnectClient();
    }
  }

  private void ClientReceiveLoop()
  {
    try
    {
      var ws = _client!;
      var token = _clientCts!.Token;
      while (_clientRunning)
      {
        var message = ReceiveMessage(ws, token);
        if (message is null || message.Length < 1)
        {
          break;
        }

        Transport.DeliverClient(message[0], Payload(message));
      }
    }
    catch
    {
      // fall through
    }

    _clientRunning = false;
  }

  // -- WebSocket helpers --

  // Reads the HTTP/1.1 Upgrade request, replies with the 101 Switching Protocols handshake, and leaves
  // the stream positioned at the first WebSocket frame. Returns false if the request isn't a valid
  // WebSocket upgrade.
  private static bool PerformServerHandshake(NetworkStream stream)
  {
    var request = ReadHttpHeaders(stream);
    if (request is null)
    {
      return false;
    }

    string? key = null;
    foreach (var line in request)
    {
      var sep = line.IndexOf(':');
      if (sep < 0)
      {
        continue;
      }

      if (line.AsSpan(0, sep).Trim().Equals("Sec-WebSocket-Key", StringComparison.OrdinalIgnoreCase))
      {
        key = line[(sep + 1)..].Trim();
        break;
      }
    }

    if (string.IsNullOrEmpty(key))
    {
      return false;
    }

    var accept = Convert.ToBase64String(SHA1.HashData(Encoding.ASCII.GetBytes(key + WebSocketGuid)));
    var response =
      "HTTP/1.1 101 Switching Protocols\r\n" +
      "Upgrade: websocket\r\n" +
      "Connection: Upgrade\r\n" +
      $"Sec-WebSocket-Accept: {accept}\r\n\r\n";

    var bytes = Encoding.ASCII.GetBytes(response);
    stream.Write(bytes, 0, bytes.Length);
    return true;
  }

  // Reads request lines up to (and consuming) the terminating blank line. Reads one byte at a time so we
  // never swallow bytes belonging to the first WebSocket frame.
  private static List<string>? ReadHttpHeaders(Stream stream)
  {
    var lines = new List<string>();
    var line = new StringBuilder();
    var buffer = new byte[1];

    while (true)
    {
      var n = stream.Read(buffer, 0, 1);
      if (n <= 0)
      {
        return null;
      }

      var c = (char)buffer[0];
      if (c == '\r')
      {
        continue;
      }

      if (c == '\n')
      {
        if (line.Length == 0)
        {
          return lines;
        }

        lines.Add(line.ToString());
        line.Clear();
        continue;
      }

      line.Append(c);

      // Guard against an unbounded request from a misbehaving or malicious client.
      if (line.Length > 8192 || lines.Count > 100)
      {
        return null;
      }
    }
  }

  // Receives one whole WebSocket message (reassembling fragments) as [type byte][payload]. Returns null
  // on close or error.
  private static byte[]? ReceiveMessage(WebSocket ws, CancellationToken token)
  {
    var buffer = new byte[8192];
    using var assembled = new MemoryStream();

    while (true)
    {
      WebSocketReceiveResult result;
      try
      {
        result = ws.ReceiveAsync(new ArraySegment<byte>(buffer), token).GetAwaiter().GetResult();
      }
      catch
      {
        return null;
      }

      if (result.MessageType == WebSocketMessageType.Close)
      {
        return null;
      }

      assembled.Write(buffer, 0, result.Count);

      if (result.EndOfMessage)
      {
        break;
      }
    }

    return assembled.ToArray();
  }

  private void SendHandshake(byte role, string token)
  {
    var tokenBytes = Encoding.UTF8.GetBytes(token);

    var message = new byte[2 + tokenBytes.Length];
    message[0] = HandshakeType;
    message[1] = role;
    Array.Copy(tokenBytes, 0, message, 2, tokenBytes.Length);

    SendRaw(_client!, _clientSendLock, message, _clientCts!.Token);
  }

  // Packs a native (type, payload) pair into a single [type byte][payload] message.
  private static byte[] BuildMessage(byte type, nint data, int len)
  {
    var message = new byte[1 + len];
    message[0] = type;
    if (len > 0)
    {
      Marshal.Copy(data, message, 1, len);
    }

    return message;
  }

  private static byte[] Payload(byte[] message)
  {
    return message.Length > 1 ? message[1..] : Array.Empty<byte>();
  }

  private static bool SendMessage(Connection conn, byte[] message)
  {
    return SendRaw(conn.Socket, conn.SendLock, message, conn.Cts.Token);
  }

  private static bool SendRaw(WebSocket ws, SemaphoreSlim sendLock, byte[] message, CancellationToken token)
  {
    sendLock.Wait();
    try
    {
      ws.SendAsync(new ArraySegment<byte>(message), WebSocketMessageType.Binary, endOfMessage: true, token)
        .GetAwaiter().GetResult();
      return true;
    }
    catch
    {
      return false;
    }
    finally
    {
      sendLock.Release();
    }
  }

  private static void Close(WebSocket ws, CancellationTokenSource? cts)
  {
    try { cts?.Cancel(); } catch { /* ignore */ }
    try { ws.Abort(); } catch { /* ignore */ }
    try { ws.Dispose(); } catch { /* ignore */ }
    cts?.Dispose();
  }
}
