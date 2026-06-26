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

// The C# socket half of ECS3DNet. The C++ side (NetServer/NetClient) owns the protocol — it only ever
// hands us an opaque (type byte + payload bytes) pair and gets the same back. Each message is framed
// on the wire as a 4-byte big-endian length (1 type byte + payload) followed by the bytes.
//
// Inbound delivery is push: C++ registers a native callback and we invoke it on the socket thread (the
// C++ MessageQueue it pushes into is thread-safe). Outbound is a direct call from the tick thread.
public static unsafe class Transport
{
  // The connection handshake is the first frame on every connection: a reserved type byte (outside the
  // C++ MessageType enum, so it's consumed here and never delivered to C++) carrying [role byte][token
  // UTF-8]. The server authorizes the connection from it before any protocol message flows.
  private const byte HandshakeType = 0xFF;
  private const byte RolePlayer = 0;
  private const byte RoleEditor = 1;

  private static delegate* unmanaged<byte, byte*, int, void> _serverReceive;
  private static delegate* unmanaged<byte, byte*, int, void> _clientReceive;

  // -- Server --
  private static TcpListener? _listener;
  private static Thread? _acceptThread;
  private static volatile bool _serverRunning;
  private static bool _editMode;

  // The token an editor must present to be granted Role.editor. Empty means an edit-mode server accepts
  // any editor (a trusted-network edit server); a non-empty token must match exactly.
  private static string _expectedToken = "";

  private static readonly List<TcpClient> _clients = new();
  private static readonly object _clientsLock = new();

  // -- Client --
  private static TcpClient? _client;
  private static Thread? _clientThread;
  private static volatile bool _clientRunning;

  [UnmanagedCallersOnly]
  public static void serverSetReceiveCallback(IntPtr fn)
  {
    _serverReceive = (delegate* unmanaged<byte, byte*, int, void>)fn;
  }

  [UnmanagedCallersOnly]
  public static void serverStart(int port, byte editMode, IntPtr expectedTokenUtf8)
  {
    if (_serverRunning)
    {
      return;
    }

    // editMode is the launch-capability gate: only an edit-mode server may grant Role.editor at the
    // handshake, and only when the presented token matches expectedToken (see Authorize).
    _editMode = editMode != 0;
    _expectedToken = Marshal.PtrToStringUTF8(expectedTokenUtf8) ?? "";

    _listener = new TcpListener(IPAddress.Any, port);
    _listener.Start();
    _serverRunning = true;

    _acceptThread = new Thread(AcceptLoop) { IsBackground = true, Name = "ecs3d-net-accept" };
    _acceptThread.Start();

    Console.WriteLine($"[Transport] Server listening on port {port} (editMode={_editMode}).");
  }

  [UnmanagedCallersOnly]
  public static void serverStop()
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

  [UnmanagedCallersOnly]
  public static int serverConnectionCount()
  {
    lock (_clientsLock)
    {
      return _clients.Count;
    }
  }

  [UnmanagedCallersOnly]
  public static void serverBroadcast(byte type, IntPtr data, int len)
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

  private static void AcceptLoop()
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

      lock (_clientsLock)
      {
        _clients.Add(client);
      }

      var thread = new Thread(() => ServerReceiveLoop(client))
      {
        IsBackground = true,
        Name = "ecs3d-net-client"
      };
      thread.Start();
    }
  }

  private static void ServerReceiveLoop(TcpClient client)
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

          Deliver(_serverReceive, type, payload);
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
  }

  // Decides whether a connection presenting this handshake payload ([role byte][token UTF-8]) is
  // allowed onto the server at all. Players connect freely. An editor is admitted too — even against a
  // non-edit server, where it gets a read-only view (the server simply honors no edits from it). The one
  // hard rejection is a real auth failure: an editor offering the wrong token to an edit server that
  // configured one. Whether an admitted editor may actually edit is conveyed separately via editStatus.
  private static bool Authorize(byte[] payload)
  {
    if (payload.Length < 1)
    {
      return false;
    }

    var role = payload[0];
    var token = payload.Length > 1 ? Encoding.UTF8.GetString(payload, 1, payload.Length - 1) : "";

    if (role == RoleEditor && _editMode && _expectedToken.Length != 0 && token != _expectedToken)
    {
      return false;
    }

    return true;
  }

  [UnmanagedCallersOnly]
  public static void clientSetReceiveCallback(IntPtr fn)
  {
    _clientReceive = (delegate* unmanaged<byte, byte*, int, void>)fn;
  }

  [UnmanagedCallersOnly]
  public static byte clientConnect(IntPtr hostUtf8, int port, byte role, IntPtr tokenUtf8)
  {
    if (_clientRunning)
    {
      return 1;
    }

    var host = Marshal.PtrToStringUTF8(hostUtf8) ?? "127.0.0.1";
    var token = Marshal.PtrToStringUTF8(tokenUtf8) ?? "";

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

  [UnmanagedCallersOnly]
  public static void clientDisconnect()
  {
    DisconnectClient();
  }

  // [UnmanagedCallersOnly] methods can't be called from managed code, so the teardown lives here and
  // both the export and clientSend's error path call it.
  private static void DisconnectClient()
  {
    _clientRunning = false;

    try { _client?.Close(); } catch { /* already closed */ }
    _client = null;
  }

  [UnmanagedCallersOnly]
  public static void clientSend(byte type, IntPtr data, int len)
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

  private static void ClientReceiveLoop()
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

        Deliver(_clientReceive, type, payload);
      }
    }
    catch
    {
      // fall through
    }

    _clientRunning = false;
  }

  // -- Framing helpers --

  private static void SendHandshake(byte role, string token)
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

  private static byte[] Frame(byte type, IntPtr data, int len)
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

  private static void Deliver(delegate* unmanaged<byte, byte*, int, void> callback, byte type, byte[] payload)
  {
    if (callback == null)
    {
      return;
    }

    if (payload.Length == 0)
    {
      callback(type, null, 0);
      return;
    }

    fixed (byte* p = payload)
    {
      callback(type, p, payload.Length);
    }
  }
}
