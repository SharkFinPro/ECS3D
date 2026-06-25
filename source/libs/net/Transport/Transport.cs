using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
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
  private delegate* unmanaged<byte, byte*, int, void> _serverReceive;
  private delegate* unmanaged<byte, byte*, int, void> _clientReceive;

  // -- Server --
  private static TcpListener? _listener;
  private static Thread? _acceptThread;
  private static volatile bool _serverRunning;
  private static bool _editMode;

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
  public static void serverStart(int port, byte editMode)
  {
    if (_serverRunning)
    {
      return;
    }

    // editMode is the launch-capability gate: only an edit-mode server may grant Role.editor at the
    // handshake. TODO: enforce it once the handshake carries role + token.
    _editMode = editMode != 0;

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
      while (_serverRunning)
      {
        if (!ReadFrame(stream, out var type, out var payload))
        {
          break;
        }

        Deliver(_serverReceive, type, payload);
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

    // role + token are sent at the handshake so the server can authorize Role.editor. TODO: write the
    // handshake frame once the server enforces the edit gate; for now the connection is anonymous.
    _ = role;
    _ = Marshal.PtrToStringUTF8(tokenUtf8);

    try
    {
      _client = new TcpClient();
      _client.Connect(host, port);
      _client.NoDelay = true;
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
      clientDisconnect();
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
