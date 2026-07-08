using System;
using System.Runtime.InteropServices;

namespace ECS3DNetTransport;

// The C# socket half of ECS3DNet. The C++ side (NetServer/NetClient) owns the protocol — it only ever
// hands us an opaque (type byte + payload bytes) pair and gets the same back.
//
// This class is just the native boundary: it holds the inbound callbacks C++ registers, exposes the
// [UnmanagedCallersOnly] exports C++ calls, and forwards every call to the selected TransportBackend.
// The actual socket work (TCP vs. WebSocket, and later UDP) lives in the backends, which are fully
// separate code paths. The wire transport is chosen by the hardcoded Protocol field below.
public static unsafe class Transport
{
  // Hardcoded transport selection. Flip this to switch the wire protocol for the whole process.
  // UDP will be added later (see TransportProtocol).
//   private static readonly TransportProtocol Protocol = TransportProtocol.WebSocket;
  private static readonly TransportProtocol Protocol = TransportProtocol.Tcp;

  // The server callback carries the origin connection id (the C++ side routes per-client input by it);
  // the client callback has a single peer and needs none.
  private static delegate* unmanaged<int, byte, byte*, int, void> _serverReceive;
  private static delegate* unmanaged<byte, byte*, int, void> _clientReceive;

  private static readonly TransportBackend _backend = CreateBackend();

  private static TransportBackend CreateBackend()
  {
    return Protocol switch
    {
      TransportProtocol.Tcp => new TcpBackend(),
      TransportProtocol.WebSocket => new WebSocketBackend(),
      _ => throw new NotSupportedException($"Transport protocol {Protocol} is not implemented yet."),
    };
  }

  [UnmanagedCallersOnly]
  public static void serverSetReceiveCallback(IntPtr fn)
  {
    _serverReceive = (delegate* unmanaged<int, byte, byte*, int, void>)fn;
  }

  [UnmanagedCallersOnly]
  public static void serverStart(int port, byte editMode, IntPtr expectedTokenUtf8)
  {
    _backend.ServerStart(port, editMode != 0, Marshal.PtrToStringUTF8(expectedTokenUtf8) ?? "");
  }

  [UnmanagedCallersOnly]
  public static void serverStop()
  {
    _backend.ServerStop();
  }

  [UnmanagedCallersOnly]
  public static int serverConnectionCount()
  {
    return _backend.ServerConnectionCount();
  }

  [UnmanagedCallersOnly]
  public static void serverBroadcast(byte type, IntPtr data, int len)
  {
    _backend.ServerBroadcast(type, data, len);
  }

  [UnmanagedCallersOnly]
  public static void clientSetReceiveCallback(IntPtr fn)
  {
    _clientReceive = (delegate* unmanaged<byte, byte*, int, void>)fn;
  }

  [UnmanagedCallersOnly]
  public static byte clientConnect(IntPtr hostUtf8, int port, byte role, IntPtr tokenUtf8)
  {
    var host = Marshal.PtrToStringUTF8(hostUtf8) ?? "127.0.0.1";
    var token = Marshal.PtrToStringUTF8(tokenUtf8) ?? "";
    return _backend.ClientConnect(host, port, role, token);
  }

  [UnmanagedCallersOnly]
  public static void clientDisconnect()
  {
    _backend.ClientDisconnect();
  }

  [UnmanagedCallersOnly]
  public static void clientSend(byte type, IntPtr data, int len)
  {
    _backend.ClientSend(type, data, len);
  }

  // -- Delivery into C++ (shared by every backend) --
  // Backends call these from their socket threads to push an inbound (type, payload) up to C++. The C++
  // MessageQueue behind the callback is thread-safe.

  internal static void DeliverServer(int connId, byte type, byte[] payload)
  {
    var callback = _serverReceive;
    if (callback == null)
    {
      return;
    }

    if (payload.Length == 0)
    {
      callback(connId, type, null, 0);
      return;
    }

    fixed (byte* p = payload)
    {
      callback(connId, type, p, payload.Length);
    }
  }

  internal static void DeliverClient(byte type, byte[] payload)
  {
    Deliver(_clientReceive, type, payload);
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
