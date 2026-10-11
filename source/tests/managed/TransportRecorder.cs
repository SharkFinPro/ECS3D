using System;
using System.Collections.Concurrent;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using ECS3DNetTransport;

namespace ECS3DManagedTests;

internal readonly record struct ReceivedMessage(int ConnId, byte Type, int Length, byte[]? Payload);

// Stands in for the C++ side of the transport: registers [UnmanagedCallersOnly] callbacks with Transport's
// process-wide statics and records what the backends deliver. Tests that use it must share one
// non-parallel xUnit collection, since those statics are shared by the whole process.
internal static unsafe class TransportRecorder
{
  // Payloads larger than this are counted but not copied, so a multi-megabyte message costs nothing here.
  private const int MaxStoredPayloadBytes = 1 << 20;

  public static readonly ConcurrentQueue<ReceivedMessage> ServerReceived = new();
  public static readonly ConcurrentQueue<ReceivedMessage> ClientReceived = new();
  public static readonly ConcurrentQueue<int> ServerDisconnected = new();
  public static readonly ConcurrentQueue<(int ConnId, byte Role)> ServerAuthorized = new();
  public static readonly ConcurrentQueue<string> Logs = new();
  private static int _clientDisconnects;

  public static int ClientDisconnects => Volatile.Read(ref _clientDisconnects);

  public static void Register()
  {
    delegate* unmanaged<IntPtr, void> setServerReceive = &Transport.serverSetReceiveCallback;
    delegate* unmanaged<IntPtr, void> setServerDisconnect = &Transport.serverSetDisconnectCallback;
    delegate* unmanaged<IntPtr, void> setServerAuthorized = &Transport.serverSetAuthorizedCallback;
    delegate* unmanaged<IntPtr, void> setClientReceive = &Transport.clientSetReceiveCallback;
    delegate* unmanaged<IntPtr, void> setClientDisconnect = &Transport.clientSetDisconnectCallback;
    delegate* unmanaged<IntPtr, void> setLog = &Transport.setLogCallback;

    setServerReceive((IntPtr)(delegate* unmanaged<int, byte, byte*, int, void>)&OnServerReceive);
    setServerDisconnect((IntPtr)(delegate* unmanaged<int, void>)&OnServerDisconnect);
    setServerAuthorized((IntPtr)(delegate* unmanaged<int, byte, void>)&OnServerAuthorized);
    setClientReceive((IntPtr)(delegate* unmanaged<byte, byte*, int, void>)&OnClientReceive);
    setClientDisconnect((IntPtr)(delegate* unmanaged<void>)&OnClientDisconnect);
    setLog((IntPtr)(delegate* unmanaged<int, IntPtr, void>)&OnLog);
  }

  public static void Unregister()
  {
    delegate* unmanaged<IntPtr, void> setServerReceive = &Transport.serverSetReceiveCallback;
    delegate* unmanaged<IntPtr, void> setServerDisconnect = &Transport.serverSetDisconnectCallback;
    delegate* unmanaged<IntPtr, void> setServerAuthorized = &Transport.serverSetAuthorizedCallback;
    delegate* unmanaged<IntPtr, void> setClientReceive = &Transport.clientSetReceiveCallback;
    delegate* unmanaged<IntPtr, void> setClientDisconnect = &Transport.clientSetDisconnectCallback;
    delegate* unmanaged<IntPtr, void> setLog = &Transport.setLogCallback;

    setServerReceive(IntPtr.Zero);
    setServerDisconnect(IntPtr.Zero);
    setServerAuthorized(IntPtr.Zero);
    setClientReceive(IntPtr.Zero);
    setClientDisconnect(IntPtr.Zero);
    setLog(IntPtr.Zero);
  }

  public static void Reset()
  {
    ServerReceived.Clear();
    ClientReceived.Clear();
    ServerDisconnected.Clear();
    ServerAuthorized.Clear();
    Logs.Clear();
    Interlocked.Exchange(ref _clientDisconnects, 0);
  }

  public static bool LogContains(string text)
  {
    return Logs.ToArray().Any(line => line.Contains(text, StringComparison.Ordinal));
  }

  public static void ClientSend(TransportBackend client, byte type, byte[] payload)
  {
    fixed (byte* data = payload)
    {
      client.ClientSend(type, (nint)data, payload.Length);
    }
  }

  public static void ServerBroadcast(TransportBackend server, byte type, byte[] payload)
  {
    fixed (byte* data = payload)
    {
      server.ServerBroadcast(type, (nint)data, payload.Length);
    }
  }

  public static void ServerSendToMany(TransportBackend server, int[] connIds, byte type, byte[] payload)
  {
    fixed (int* ids = connIds)
    fixed (byte* data = payload)
    {
      server.ServerSendToMany((nint)ids, connIds.Length, type, (nint)data, payload.Length);
    }
  }

  private static ReceivedMessage Capture(int connId, byte type, byte* data, int len)
  {
    byte[]? payload = null;
    if (len <= MaxStoredPayloadBytes)
    {
      payload = new byte[len];
      if (len > 0)
      {
        Marshal.Copy((IntPtr)data, payload, 0, len);
      }
    }

    return new ReceivedMessage(connId, type, len, payload);
  }

  [UnmanagedCallersOnly]
  private static void OnServerReceive(int connId, byte type, byte* data, int len)
  {
    ServerReceived.Enqueue(Capture(connId, type, data, len));
  }

  [UnmanagedCallersOnly]
  private static void OnServerDisconnect(int connId)
  {
    ServerDisconnected.Enqueue(connId);
  }

  [UnmanagedCallersOnly]
  private static void OnServerAuthorized(int connId, byte role)
  {
    ServerAuthorized.Enqueue((connId, role));
  }

  [UnmanagedCallersOnly]
  private static void OnClientReceive(byte type, byte* data, int len)
  {
    ClientReceived.Enqueue(Capture(0, type, data, len));
  }

  [UnmanagedCallersOnly]
  private static void OnClientDisconnect()
  {
    Interlocked.Increment(ref _clientDisconnects);
  }

  [UnmanagedCallersOnly]
  private static void OnLog(int level, IntPtr message)
  {
    Logs.Enqueue(Marshal.PtrToStringUTF8(message) ?? "");
  }
}
