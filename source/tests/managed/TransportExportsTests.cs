using System;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Threading;
using ECS3DNetTransport;
using Xunit;

namespace ECS3DManagedTests;

// Calls Transport's [UnmanagedCallersOnly] exports through function pointers, the way the native side does,
// against the process-wide backend. The test owns both its server and client halves, which is why it shares
// the serialized "Transport" collection with the other tests that touch those statics.
[Collection("Transport")]
public sealed unsafe class TransportExportsTests : IDisposable
{
  private const int WaitMs = 5000;

  public TransportExportsTests()
  {
    TransportRecorder.Register();
    TransportRecorder.Reset();
  }

  public void Dispose()
  {
    try
    {
      delegate* unmanaged<void> clientDisconnect = &Transport.clientDisconnect;
      clientDisconnect();
    }
    catch
    {
      // best effort
    }

    try
    {
      delegate* unmanaged<void> serverStop = &Transport.serverStop;
      serverStop();
    }
    catch
    {
      // best effort
    }

    TransportRecorder.Unregister();
  }

  private static int FreePort()
  {
    var listener = new TcpListener(IPAddress.Any, 0);
    listener.Start();
    var port = ((IPEndPoint)listener.LocalEndpoint).Port;
    listener.Stop();
    return port;
  }

  private static void WaitFor(Func<bool> condition, string what)
  {
    var deadline = Environment.TickCount64 + WaitMs;
    while (!condition())
    {
      Assert.True(Environment.TickCount64 < deadline, $"Timed out waiting for {what}.");
      Thread.Sleep(5);
    }
  }

  private static int ConnectionCount()
  {
    delegate* unmanaged<int> serverConnectionCount = &Transport.serverConnectionCount;
    return serverConnectionCount();
  }

  [Fact]
  public void Exports_RouteAServerAndAClientThroughTheSelectedBackend()
  {
    delegate* unmanaged<int, byte, IntPtr, void> serverStart = &Transport.serverStart;
    delegate* unmanaged<void> serverStop = &Transport.serverStop;
    delegate* unmanaged<byte, IntPtr, int, void> serverBroadcast = &Transport.serverBroadcast;
    delegate* unmanaged<IntPtr, int, byte, IntPtr, int, void> serverSendToMany = &Transport.serverSendToMany;
    delegate* unmanaged<IntPtr, int, byte, IntPtr, byte> clientConnect = &Transport.clientConnect;
    delegate* unmanaged<void> clientDisconnect = &Transport.clientDisconnect;
    delegate* unmanaged<byte, IntPtr, int, void> clientSend = &Transport.clientSend;

    // Probed on Any because the backend binds Any, and a bind failure inside the export is fatal to the
    // test host (it cannot be retried), so the window is kept small by probing right before the call.
    var port = FreePort();
    var serverToken = Marshal.StringToCoTaskMemUTF8("secret");
    var clientToken = Marshal.StringToCoTaskMemUTF8("");
    var host = Marshal.StringToCoTaskMemUTF8("127.0.0.1");
    byte connected;
    try
    {
      serverStart(port, 1, serverToken);
      connected = clientConnect(host, port, 0, clientToken);
    }
    finally
    {
      Marshal.FreeCoTaskMem(serverToken);
      Marshal.FreeCoTaskMem(clientToken);
      Marshal.FreeCoTaskMem(host);
    }

    Assert.Equal(1, connected);

    WaitFor(() => ConnectionCount() == 1, "the client to be listed");
    WaitFor(() => TransportRecorder.ServerAuthorized.Count == 1, "the client to be authorized");
    var (connId, role) = TransportRecorder.ServerAuthorized.Single();
    Assert.Equal(0, role);

    var message = new byte[] { 1, 2, 3 };
    fixed (byte* data = message)
    {
      clientSend(5, (IntPtr)data, message.Length);
    }

    WaitFor(() => TransportRecorder.ServerReceived.Count == 1, "the server to receive the client's message");
    var received = TransportRecorder.ServerReceived.Single();
    Assert.Equal(connId, received.ConnId);
    Assert.Equal(5, received.Type);
    Assert.Equal(message, received.Payload);

    var broadcast = new byte[] { 9, 8 };
    fixed (byte* data = broadcast)
    {
      serverBroadcast(6, (IntPtr)data, broadcast.Length);
    }

    WaitFor(() => TransportRecorder.ClientReceived.Count == 1, "the client to receive the broadcast");
    Assert.Equal(6, TransportRecorder.ClientReceived.Single().Type);
    Assert.Equal(broadcast, TransportRecorder.ClientReceived.Single().Payload);

    var named = new byte[] { 4 };
    var ids = new[] { connId };
    fixed (int* idData = ids)
    fixed (byte* data = named)
    {
      serverSendToMany((IntPtr)idData, ids.Length, 7, (IntPtr)data, named.Length);
    }

    WaitFor(() => TransportRecorder.ClientReceived.Count == 2, "the client to receive the named send");
    Assert.Equal(7, TransportRecorder.ClientReceived.ToArray()[1].Type);

    clientDisconnect();
    WaitFor(() => TransportRecorder.ServerDisconnected.Contains(connId), "the server to see the client go");
    WaitFor(() => ConnectionCount() == 0, "the server to drop the connection");

    serverStop();
  }

  [Fact]
  public void Delivery_WithoutRegisteredCallbacks_DoesNothing_AndReachesTheRecorderOnceRegistered()
  {
    TransportRecorder.Unregister();

    var empty = Array.Empty<byte>();
    Transport.DeliverServer(1, 2, new byte[] { 1 });
    Transport.DeliverServer(1, 2, empty);
    Transport.DeliverClient(3, new byte[] { 1 });
    Transport.DeliverClient(3, empty);
    Transport.DeliverServerDisconnect(1);
    Transport.DeliverServerAuthorized(1, 2);
    Transport.DeliverClientDisconnect();

    Assert.Empty(TransportRecorder.ServerReceived);
    Assert.Empty(TransportRecorder.ClientReceived);
    Assert.Empty(TransportRecorder.ServerDisconnected);
    Assert.Empty(TransportRecorder.ServerAuthorized);
    Assert.Equal(0, TransportRecorder.ClientDisconnects);

    TransportRecorder.Register();

    Transport.DeliverServer(1, 2, new byte[] { 7, 8 });
    Transport.DeliverServer(4, 5, empty);
    Transport.DeliverClient(3, new byte[] { 9 });
    Transport.DeliverClient(6, empty);
    Transport.DeliverServerDisconnect(11);
    Transport.DeliverServerAuthorized(12, 1);
    Transport.DeliverClientDisconnect();

    var server = TransportRecorder.ServerReceived.ToArray();
    Assert.Equal(2, server.Length);
    Assert.Equal(1, server[0].ConnId);
    Assert.Equal(2, server[0].Type);
    Assert.Equal(new byte[] { 7, 8 }, server[0].Payload);
    Assert.Equal(4, server[1].ConnId);
    Assert.Equal(5, server[1].Type);
    Assert.Equal(0, server[1].Length);

    var client = TransportRecorder.ClientReceived.ToArray();
    Assert.Equal(2, client.Length);
    Assert.Equal(3, client[0].Type);
    Assert.Equal(new byte[] { 9 }, client[0].Payload);
    Assert.Equal(6, client[1].Type);
    Assert.Equal(0, client[1].Length);

    Assert.Equal(new[] { 11 }, TransportRecorder.ServerDisconnected.ToArray());
    Assert.Equal(new[] { (12, (byte)1) }, TransportRecorder.ServerAuthorized.ToArray());
    Assert.Equal(1, TransportRecorder.ClientDisconnects);
  }

  [Fact]
  public void Log_FallsBackToTheConsoleUntilACallbackIsRegistered()
  {
    var originalOut = Console.Out;
    var originalError = Console.Error;
    var capturedOut = new StringWriter();
    var capturedError = new StringWriter();

    TransportRecorder.Unregister();
    try
    {
      Console.SetOut(capturedOut);
      Console.SetError(capturedError);

      Transport.Log(TransportLogLevel.Info, "a-info-line");
      Transport.Log(TransportLogLevel.Warn, "b-warn-line");
    }
    finally
    {
      Console.SetOut(originalOut);
      Console.SetError(originalError);
    }

    Assert.Contains("a-info-line", capturedOut.ToString());
    Assert.DoesNotContain("b-warn-line", capturedOut.ToString());
    Assert.Contains("b-warn-line", capturedError.ToString());
    Assert.DoesNotContain("a-info-line", capturedError.ToString());

    TransportRecorder.Register();
    try
    {
      Console.SetOut(capturedOut = new StringWriter());
      Console.SetError(capturedError = new StringWriter());

      Transport.Log(TransportLogLevel.Info, "c-registered-line");
    }
    finally
    {
      Console.SetOut(originalOut);
      Console.SetError(originalError);
    }

    Assert.True(TransportRecorder.LogContains("c-registered-line"));
    Assert.DoesNotContain("c-registered-line", capturedOut.ToString());
    Assert.DoesNotContain("c-registered-line", capturedError.ToString());
  }
}
