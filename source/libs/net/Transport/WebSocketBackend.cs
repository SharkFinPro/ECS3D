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
  // it throttles to ~one per 15.6ms - a per-message stall that made WebSocket feel laggy versus TCP.
  //
  //  - A send buffer larger than a typical message keeps the common case on the synchronous path.
  //  - Raising the thread-pool minimum removes the injection delay for the rare oversized message that
  //    still goes async, so it degrades to plain bandwidth cost (same as TCP) instead of a 15ms hitch.
  private const int SocketBufferSize = 1 << 20; // 1 MiB per direction.

  // What one whole broadcast may spend blocking on peers that have stopped draining their sockets, not
  // what each peer may spend: the budget is shared across the fan-out, so ten stalled peers cost this
  // once rather than ten times over. SendAsync completes only once the message has reached the socket's
  // send buffer, and broadcasts run on the tick thread, so the bound is what physics and scripts can lose
  // to the network in a tick. Only the connection whose own send ran out of budget is dropped; peers the
  // broadcast never reached are skipped for that message and kept. Two seconds because a stall then costs
  // at most one tick's worth before the peer responsible is dropped, while still being far longer than any
  // plausible snapshot send takes on a healthy link.
  private const int SendTimeoutMs = 2000;

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

  // Comfortably under the callers' 15 s retry budget so several attempts fit, and long enough for a real
  // WAN handshake. Unbounded, one attempt against a host that routes but never answers runs to the OS
  // connect timeout (~21 s on Windows) and outlives the whole budget on its own.
  private const int ConnectTimeoutMs = 3000;

  // A WebSocket is safe for one concurrent send and one concurrent receive, but not for concurrent sends.
  // The send lock serializes broadcasts (and any future sender) onto a single connection.
  private sealed class Connection(WebSocket socket, CancellationTokenSource cts, int connId)
  {
    public readonly WebSocket Socket = socket;
    public readonly CancellationTokenSource Cts = cts;
    public readonly SemaphoreSlim SendLock = new(1, 1);
    // The id C++ knows this connection by, so a send that fails on the broadcast path can name it.
    public readonly int ConnId = connId;
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

    Transport.Log(TransportLogLevel.Info, $"WebSocket server listening on port {port} (editMode={EditMode}).");
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
    if (TooLargeToSend(len))
    {
      return;
    }

    var message = BuildMessage(type, data, len);

    // The fan-out runs off a snapshot so the sends happen outside _clientsLock. This is the tick thread,
    // and a send to a peer that has stopped reading blocks until the budget below runs out; holding the
    // lock across that would block the accept path and every receive loop's cleanup along with it.
    Connection[] connections;
    lock (_clientsLock)
    {
      connections = _connections.ToArray();
    }

    // One budget for the whole fan-out. A per-connection timeout would still let K stalled peers hold the
    // tick thread for K times the timeout in a single broadcast, which is the stall this avoids.
    var deadline = Environment.TickCount64 + SendTimeoutMs;
    var skipped = 0;

    foreach (var conn in connections)
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

      // The connection dropped mid-send, or did not accept the message inside the remaining budget.
      if (!SendMessage(conn, message, (int)remaining) && Reap(conn))
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
  // Closing makes the connection's blocked receive return, and HandleClient's cleanup owns the
  // DeliverServerDisconnect for it - delivering one here too would free the player slot twice. Closing an
  // already-closed connection is harmless.
  private bool Reap(Connection conn)
  {
    Close(conn.Socket, conn.Cts);

    lock (_clientsLock)
    {
      // By reference rather than by index: the list may have changed since the broadcast's snapshot.
      return _connections.Remove(conn);
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
    var connId = 0;

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
      // The handshake gets the tighter ceiling: it is the one message read before the peer is authorized.
      var first = ReceiveMessage(ws, cts.Token, MaxHandshakeBytes);
      var handshakePayload = first is null ? null : Payload(first);
      if (first is null || first.Length < 1 || first[0] != HandshakeType || !Authorize(handshakePayload!))
      {
        Transport.Log(TransportLogLevel.Warn, "Rejected a connection that failed the handshake.");
        return;
      }

      connId = Interlocked.Increment(ref _nextConnId);

      // Authorize succeeded: tell the native side which role this connection was actually granted, so it
      // can enforce that role on every later message rather than trusting one the sender claims.
      Transport.DeliverServerAuthorized(connId, handshakePayload![0]);

      conn = new Connection(ws, cts, connId);
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

        // Release any player slot the server bound to this connection (only admitted connections, which
        // are the ones that got a connId, reach here with conn != null).
        Transport.DeliverServerDisconnect(connId);
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

    // Held out here so a failure before the fields below take ownership can still dispose them.
    ClientWebSocket? ws = null;
    HttpMessageInvoker? invoker = null;

    try
    {
      ws = new ClientWebSocket();
      ws.Options.KeepAliveInterval = KeepAlive;

      // ClientWebSocket gives no way to set NoDelay on its socket, so it would otherwise leave Nagle's
      // algorithm enabled - small per-tick messages get held ~40ms (Nagle + delayed ACK), the lag the
      // TCP backend avoids by setting NoDelay on both ends. A ConnectCallback lets us own the socket and
      // disable Nagle ourselves.
      invoker = new HttpMessageInvoker(new SocketsHttpHandler
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

      // Scoped to the connect alone: disposing the source kills its pending timer, so a slow but
      // successful connect cannot be aborted once the connection is live.
      using (var connectCts = new CancellationTokenSource(ConnectTimeoutMs))
      {
        ws.ConnectAsync(new Uri($"ws://{host}:{port}/"), invoker, connectCts.Token).GetAwaiter().GetResult();
      }

      _client = ws;
      _clientInvoker = invoker;
      _clientCts = new CancellationTokenSource();

      // Send role + token as the first message so the server can authorize this connection (in
      // particular grant Role.editor) before any protocol message. Same wire format everywhere.
      SendHandshake(role, token);
    }
    catch (OperationCanceledException)
    {
      Transport.Log(TransportLogLevel.Warn, $"Connect to {host}:{port} timed out after {ConnectTimeoutMs} ms.");
      DisconnectClient();
      DisposeClientPieces(ws, invoker);
      return 0;
    }
    catch (Exception e)
    {
      Transport.Log(TransportLogLevel.Warn, $"Client failed to connect to {host}:{port}: {e.Message}");
      DisconnectClient();
      DisposeClientPieces(ws, invoker);
      return 0;
    }

    _clientRunning = true;

    _clientThread = new Thread(ClientReceiveLoop) { IsBackground = true, Name = "ecs3d-net-recv" };
    _clientThread.Start();

    Transport.Log(TransportLogLevel.Info, $"Connected to {host}:{port}.");
    return 1;
  }

  // A connect that fails before the client fields take ownership leaves the socket and its invoker as
  // locals that DisconnectClient cannot see, so a retry loop would otherwise drop a pair per attempt.
  // Safe to call once the fields did take them: both disposals are idempotent.
  private static void DisposeClientPieces(ClientWebSocket? ws, HttpMessageInvoker? invoker)
  {
    try { ws?.Dispose(); } catch { /* ignore */ }
    try { invoker?.Dispose(); } catch { /* ignore */ }
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
    if (ws is null || cts is null || TooLargeToSend(len))
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

    // The single delivery point for a lost connection, whether the peer closed it, a read failed, or
    // ClientDisconnect closed our own socket to make this loop exit - the native side tells the two
    // apart via m_disconnectRequested and no-ops the latter.
    Transport.DeliverClientDisconnect();
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
  private static byte[]? ReceiveMessage(WebSocket ws, CancellationToken token, int maxBytes = MaxMessageBytes)
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

      // A fragmented message has no declared length, so the ceiling is on what has actually arrived: a
      // peer can otherwise keep sending continuation frames and never set the end bit, and the stream
      // grows until the process gives out. Slower than the TCP case, and the same ending.
      if (assembled.Length + result.Count > maxBytes)
      {
        Transport.Log(TransportLogLevel.Warn, $"Refused a message past {maxBytes} bytes; the peer kept sending.");

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

  // Bounded by a linked source so a peer that has stopped reading cannot hold the tick thread: the send is
  // cancelled once timeoutMs (what is left of the broadcast's budget) elapses, which fails it and takes
  // the caller's close-and-reap path.
  private static bool SendMessage(Connection conn, byte[] message, int timeoutMs)
  {
    CancellationTokenSource? linked = null;
    try
    {
      linked = CancellationTokenSource.CreateLinkedTokenSource(conn.Cts.Token);
      linked.CancelAfter(timeoutMs);

      return SendRaw(conn.Socket, conn.SendLock, message, linked.Token);
    }
    catch
    {
      // Reading Cts.Token throws once the receive loop has closed and disposed this connection; there is
      // nothing left to send to, so report the failure and let the caller drop it.
      return false;
    }
    finally
    {
      linked?.Dispose();
    }
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
    try { cts?.Dispose(); } catch { /* ignore */ }
  }
}
