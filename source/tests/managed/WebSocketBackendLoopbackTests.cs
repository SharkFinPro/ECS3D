using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using ECS3DNetTransport;
using Xunit;

namespace ECS3DManagedTests;

// Drives a real WebSocketBackend over loopback sockets. Every wait polls a condition against a deadline so
// a regression fails with a message instead of hanging the run; the timeouts a test needs to see fire are
// shortened through the backend's internal fields.
[Collection("Transport")]
public sealed class WebSocketBackendLoopbackTests : IDisposable
{
  private const byte RolePlayer = 0;
  private const byte RoleEditor = 1;
  private const byte HandshakeType = 0xFF;
  private const int WaitMs = 5000;
  private const int MaxStartAttempts = 5;

  private readonly List<WebSocketBackend> _servers = new();
  private readonly List<WebSocketBackend> _clients = new();
  private readonly List<RawPeer> _peers = new();

  public WebSocketBackendLoopbackTests()
  {
    TransportRecorder.Register();
    TransportRecorder.Reset();
  }

  public void Dispose()
  {
    foreach (var client in _clients)
    {
      try { client.ClientReceiveLoopExiting = null; } catch { /* best effort */ }
      try { client.ClientDisconnect(); } catch { /* best effort */ }
    }

    foreach (var peer in _peers)
    {
      peer.Dispose();
    }

    foreach (var server in _servers)
    {
      try { server.ServerStop(); } catch { /* best effort */ }
    }

    TransportRecorder.Unregister();
  }

  // A ClientWebSocket the test drives by hand, so it controls exactly when (and whether) the peer reads
  // or writes. The connect callback owns the socket so the receive buffer can be made small.
  private sealed class RawPeer : IDisposable
  {
    private readonly ClientWebSocket _socket = new();
    private readonly HttpMessageInvoker _invoker;

    public RawPeer(int port, byte role, string token, int? receiveBufferBytes, bool sendHandshake)
    {
      _invoker = new HttpMessageInvoker(new SocketsHttpHandler
      {
        ConnectCallback = async (context, ct) =>
        {
          var socket = new Socket(SocketType.Stream, ProtocolType.Tcp) { NoDelay = true };
          if (receiveBufferBytes.HasValue)
          {
            socket.ReceiveBufferSize = receiveBufferBytes.Value;
          }

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

      using (var cts = new CancellationTokenSource(WaitMs))
      {
        _socket.ConnectAsync(new Uri($"ws://127.0.0.1:{port}/"), _invoker, cts.Token).GetAwaiter().GetResult();
      }

      if (sendHandshake)
      {
        var handshake = new[] { HandshakeType, role }.Concat(Encoding.UTF8.GetBytes(token)).ToArray();
        Send(handshake);
      }
    }

    public void Send(byte[] bytes, bool endOfMessage = true)
    {
      using var cts = new CancellationTokenSource(WaitMs);
      _socket.SendAsync(new ArraySegment<byte>(bytes), WebSocketMessageType.Binary, endOfMessage, cts.Token)
        .GetAwaiter().GetResult();
    }

    // One whole binary message, reassembled from however many frames it arrived in.
    public byte[] Receive()
    {
      using var cts = new CancellationTokenSource(WaitMs);
      var buffer = new byte[8192];
      using var assembled = new MemoryStream();

      while (true)
      {
        var result = _socket.ReceiveAsync(new ArraySegment<byte>(buffer), cts.Token).GetAwaiter().GetResult();
        Assert.Equal(WebSocketMessageType.Binary, result.MessageType);
        assembled.Write(buffer, 0, result.Count);

        if (result.EndOfMessage)
        {
          return assembled.ToArray();
        }
      }
    }

    public void Dispose()
    {
      try { _socket.Abort(); } catch { /* ignore */ }
      try { _socket.Dispose(); } catch { /* ignore */ }
      try { _invoker.Dispose(); } catch { /* ignore */ }
    }
  }

  private static int FreePort()
  {
    var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    var port = ((IPEndPoint)listener.LocalEndpoint).Port;
    listener.Stop();
    return port;
  }

  private int Start(WebSocketBackend server, bool editMode = false, string token = "")
  {
    _servers.Add(server);

    // The port is free when picked but could be taken before the bind, so retry a few times.
    for (var attempt = 1; attempt < MaxStartAttempts; ++attempt)
    {
      var port = FreePort();
      try
      {
        server.ServerStart(port, editMode, token);
        return port;
      }
      catch (SocketException)
      {
        // Another process took the probed port; pick a new one.
      }
    }

    var lastPort = FreePort();
    server.ServerStart(lastPort, editMode, token);
    return lastPort;
  }

  private WebSocketBackend Connect(int port, byte role, string token = "")
  {
    var client = new WebSocketBackend();
    _clients.Add(client);
    Assert.Equal(1, client.ClientConnect("127.0.0.1", port, role, token));
    return client;
  }

  private RawPeer ConnectRaw(int port, int? receiveBufferBytes = null, byte role = RolePlayer,
    bool sendHandshake = true)
  {
    var peer = new RawPeer(port, role, "", receiveBufferBytes, sendHandshake);
    _peers.Add(peer);
    return peer;
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

  // The negative half of a check: the count must still be `expected` after it has had time to move.
  private static void AssertStays(Func<int> count, int expected, string what)
  {
    var until = Environment.TickCount64 + 200;
    while (Environment.TickCount64 < until)
    {
      var current = count();
      Assert.True(current == expected, $"{what}: expected it to stay {expected} but it became {current}.");
      Thread.Sleep(20);
    }
  }

  private static void RunWithDeadline(Action action, string what)
  {
    var task = Task.Run(action);
    Assert.True(task.Wait(10000), $"{what} did not return.");
    task.GetAwaiter().GetResult();
  }

  private static (int ConnId, byte Role)[] Authorized()
  {
    return TransportRecorder.ServerAuthorized.ToArray();
  }

  // Connects one more player and waits until the server has put it on its broadcast list, so the list
  // order matches the order of the calls. Returns that connection's id.
  private int ConnectAndAwait(WebSocketBackend server, int port, int expectedCount)
  {
    Connect(port, RolePlayer);
    WaitFor(() => server.ServerConnectionCount() == expectedCount, $"connection {expectedCount} to join");
    return Authorized()[expectedCount - 1].ConnId;
  }

  [Fact]
  public void Handshake_ReportsTheGrantedRoleAndRefusesAWrongEditorToken()
  {
    var server = new WebSocketBackend();
    var port = Start(server, editMode: true, token: "secret");

    // The wrong token goes first so its refusal is settled before anything else is on the server. The
    // upgrade succeeds and the handshake message is what is refused, so the client's connect still returns 1
    // and its receive loop sees the server close. Unlike TcpBackend, a refused connection never gets an id
    // here, so the server delivers no disconnect for it.
    Connect(port, RoleEditor, "wrong");
    WaitFor(() => TransportRecorder.ClientDisconnects == 1, "the refused client to be told");
    Assert.Empty(Authorized());

    Connect(port, RoleEditor, "secret");
    Connect(port, RolePlayer);
    WaitFor(() => Authorized().Length == 2, "the two accepted connections to be authorized");
    WaitFor(() => server.ServerConnectionCount() == 2, "both accepted connections to be listed");

    var granted = Authorized();
    Assert.Equal(new[] { RolePlayer, RoleEditor }, granted.Select(a => a.Role).OrderBy(role => role).ToArray());
    Assert.Equal(2, granted.Select(a => a.ConnId).Distinct().Count());
    AssertStays(() => TransportRecorder.ServerDisconnected.Count, 0, "disconnects for connections never admitted");
    Assert.Equal(1, TransportRecorder.ClientDisconnects);
  }

  [Fact]
  public void ClientMessageArrivesWithItsConnectionId_AndBroadcastReachesEveryClient()
  {
    var server = new WebSocketBackend();
    var port = Start(server);

    var sender = Connect(port, RolePlayer);
    WaitFor(() => server.ServerConnectionCount() == 1, "the sender to join");
    var senderId = Authorized()[0].ConnId;
    var viewer = ConnectRaw(port);
    WaitFor(() => server.ServerConnectionCount() == 2, "the viewer to join");

    var message = new byte[] { 1, 2, 3, 4 };
    TransportRecorder.ClientSend(sender, 5, message);
    WaitFor(() => TransportRecorder.ServerReceived.Count == 1, "the server to receive the message");

    var received = TransportRecorder.ServerReceived.Single();
    Assert.Equal(senderId, received.ConnId);
    Assert.Equal(5, received.Type);
    Assert.Equal(message, received.Payload);

    // A message that is only its type byte reaches the native callback as an empty payload.
    TransportRecorder.ClientSend(sender, 6, Array.Empty<byte>());
    WaitFor(() => TransportRecorder.ServerReceived.Count == 2, "the server to receive the empty message");

    var empty = TransportRecorder.ServerReceived.ToArray()[1];
    Assert.Equal(senderId, empty.ConnId);
    Assert.Equal(6, empty.Type);
    Assert.Equal(0, empty.Length);
    Assert.Empty(empty.Payload!);

    var broadcast = new byte[] { 9, 8, 7 };
    TransportRecorder.ServerBroadcast(server, 6, broadcast);

    WaitFor(() => TransportRecorder.ClientReceived.Count == 1, "the client to receive the broadcast");
    var clientGot = TransportRecorder.ClientReceived.Single();
    Assert.Equal(6, clientGot.Type);
    Assert.Equal(broadcast, clientGot.Payload);

    Assert.Equal(new byte[] { 6, 9, 8, 7 }, viewer.Receive());
  }

  [Fact]
  public void SendToMany_ReachesOnlyTheNamedConnection()
  {
    var server = new WebSocketBackend();
    var port = Start(server);

    Connect(port, RolePlayer);
    WaitFor(() => server.ServerConnectionCount() == 1, "the first client to join");
    var viewer = ConnectRaw(port);
    WaitFor(() => server.ServerConnectionCount() == 2, "the second client to join");
    var viewerId = Authorized()[1].ConnId;

    TransportRecorder.ServerSendToMany(server, new[] { viewerId }, 11, new byte[] { 1 });
    TransportRecorder.ServerBroadcast(server, 12, new byte[] { 2 });

    // Messages arrive in order on a connection, so the viewer seeing 11 then 12 proves the named send
    // reached it; the other client seeing only the broadcast proves it was left out of the named one.
    Assert.Equal(new byte[] { 11, 1 }, viewer.Receive());
    Assert.Equal(new byte[] { 12, 2 }, viewer.Receive());

    WaitFor(() => TransportRecorder.ClientReceived.Count >= 1, "the first client to receive the broadcast");
    AssertStays(() => TransportRecorder.ClientReceived.Count, 1, "messages the first client received");
    Assert.Equal(12, TransportRecorder.ClientReceived.Single().Type);
  }

  [Fact]
  public void StalledPeer_IsDroppedWhileTheOtherPeersKeepReceiving()
  {
    // 4000 ms leaves room for two healthy peers to drain a 4 MiB message even on a loaded runner; the
    // stalled peer, last in the list, then spends the rest of the budget and is the only one dropped. The
    // server's accepted socket has a 1 MiB send buffer, so 4 MiB sent to a peer that never reads must
    // block; the loop is the backstop for platforms whose kernel buffers absorb more than that.
    var server = new WebSocketBackend { SendTimeoutMs = 4000 };
    var port = Start(server);

    var healthyA = ConnectAndAwait(server, port, 1);
    var healthyB = ConnectAndAwait(server, port, 2);

    // A tiny receive buffer and no reads, so the messages below cannot fit in the kernel's buffers.
    ConnectRaw(port, receiveBufferBytes: 1024);
    WaitFor(() => server.ServerConnectionCount() == 3, "the stalled peer to join");
    var stalledId = Authorized()[2].ConnId;

    const int bigBytes = 4 * 1024 * 1024;
    const int maxBigMessages = 8;
    var bigMessages = 0;
    while (bigMessages < maxBigMessages && !TransportRecorder.ServerDisconnected.Contains(stalledId))
    {
      TransportRecorder.ServerBroadcast(server, 7, new byte[bigBytes]);
      ++bigMessages;
    }

    WaitFor(() => TransportRecorder.ServerDisconnected.Contains(stalledId), "the stalled peer to be dropped");
    WaitFor(() => TransportRecorder.ClientReceived.Count(m => m.Length == bigBytes) == 2 * bigMessages,
      "both healthy peers to receive every large message");

    TransportRecorder.ServerBroadcast(server, 8, new byte[] { 1, 2, 3 });
    WaitFor(() => TransportRecorder.ClientReceived.Count(m => m.Type == 8) == 2,
      "both healthy peers to receive the next broadcast");

    Assert.DoesNotContain(healthyA, TransportRecorder.ServerDisconnected);
    Assert.DoesNotContain(healthyB, TransportRecorder.ServerDisconnected);
    Assert.Equal(0, TransportRecorder.ClientDisconnects);
    Assert.Equal(2, server.ServerConnectionCount());
  }

  [Fact]
  public void HandshakeFragmentedPastTheCeiling_IsRefusedOnWhatHasActuallyArrived()
  {
    var server = new WebSocketBackend();
    var port = Start(server);

    // Five 1 KiB fragments with the end bit never set: past the 4 KiB handshake ceiling, with no declared
    // length for the server to check up front.
    var attacker = ConnectRaw(port, sendHandshake: false);
    for (var i = 0; i < 5; ++i)
    {
      attacker.Send(new byte[1024], endOfMessage: false);
    }

    WaitFor(() => TransportRecorder.LogContains("Refused a message past 4096 bytes"), "the oversize refusal");
    AssertStays(() => TransportRecorder.ServerAuthorized.Count, 0, "authorizations for the oversize handshake");
    Assert.Equal(0, server.ServerConnectionCount());

    // Positive control: a normal player is admitted by the same server afterwards.
    Connect(port, RolePlayer);
    WaitFor(() => Authorized().Length == 1, "the player to be authorized");
    WaitFor(() => server.ServerConnectionCount() == 1, "the player to be listed");
  }

  [Fact]
  public void ClientConnect_FailsWhereNothingListens_AndRecoversAgainstARealServer()
  {
    var client = new WebSocketBackend();
    _clients.Add(client);

    // A refused connect surfaces as a general failure, or as the timeout if the OS is slow to refuse.
    Assert.Equal(0, client.ClientConnect("127.0.0.1", FreePort(), RolePlayer, ""));
    Assert.True(TransportRecorder.LogContains("Client failed to connect") || TransportRecorder.LogContains("timed out"));

    var server = new WebSocketBackend();
    var port = Start(server);
    Assert.Equal(1, client.ClientConnect("127.0.0.1", port, RolePlayer, ""));
    WaitFor(() => server.ServerConnectionCount() == 1, "the retried connection to join");
  }

  [Fact]
  public void ServerStop_ClosesConnectedClientsAndDeliversNothingAfterItReturns()
  {
    var server = new WebSocketBackend();
    var port = Start(server);

    var clientA = Connect(port, RolePlayer);
    WaitFor(() => server.ServerConnectionCount() == 1, "the first client to join");
    Connect(port, RolePlayer);
    WaitFor(() => server.ServerConnectionCount() == 2, "the second client to join");
    var ids = Authorized().Select(a => a.ConnId).ToArray();

    TransportRecorder.ClientSend(clientA, 5, new byte[] { 1 });
    WaitFor(() => TransportRecorder.ServerReceived.Count == 1, "a message while the server is up");

    RunWithDeadline(server.ServerStop, "ServerStop");

    Assert.Equal(0, server.ServerConnectionCount());
    Assert.Equal(ids.OrderBy(id => id), TransportRecorder.ServerDisconnected.ToArray().OrderBy(id => id));

    var received = TransportRecorder.ServerReceived.Count;
    var authorized = TransportRecorder.ServerAuthorized.Count;
    var disconnected = TransportRecorder.ServerDisconnected.Count;

    TransportRecorder.ClientSend(clientA, 5, new byte[] { 2 });
    var late = new WebSocketBackend();
    _clients.Add(late);
    Assert.Equal(0, late.ClientConnect("127.0.0.1", port, RolePlayer, ""));

    AssertStays(() => TransportRecorder.ServerReceived.Count, received, "messages received after ServerStop");
    AssertStays(() => TransportRecorder.ServerAuthorized.Count, authorized, "authorizations after ServerStop");
    AssertStays(() => TransportRecorder.ServerDisconnected.Count, disconnected, "disconnects after ServerStop");
  }

  [Fact]
  public void ClientDisconnect_ReturnsAndTheServerSeesTheConnectionGo()
  {
    var server = new WebSocketBackend();
    var port = Start(server);

    var client = Connect(port, RolePlayer);
    WaitFor(() => server.ServerConnectionCount() == 1, "the client to join");
    var id = Authorized()[0].ConnId;

    RunWithDeadline(client.ClientDisconnect, "ClientDisconnect");

    WaitFor(() => server.ServerConnectionCount() == 0, "the server to drop the connection");
    WaitFor(() => TransportRecorder.ServerDisconnected.Contains(id), "the server's disconnect callback");
  }

  [Fact]
  public void ClientWhoseServerGoesAway_IsToldExactlyOnce()
  {
    var server = new WebSocketBackend();
    var port = Start(server);

    var client = Connect(port, RolePlayer);
    WaitFor(() => server.ServerConnectionCount() == 1, "the client to join");
    Assert.Equal(0, TransportRecorder.ClientDisconnects);

    RunWithDeadline(server.ServerStop, "ServerStop");

    WaitFor(() => TransportRecorder.ClientDisconnects == 1, "the client's disconnect callback");
    AssertStays(() => TransportRecorder.ClientDisconnects, 1, "client disconnect callbacks");

    RunWithDeadline(client.ClientDisconnect, "ClientDisconnect");
    AssertStays(() => TransportRecorder.ClientDisconnects, 1, "client disconnect callbacks after ClientDisconnect");
  }

  [Fact]
  public void ReconnectRacingTheOldLoopsExit_IsNotReportedAsALoss()
  {
    var serverA = new WebSocketBackend();
    var portA = Start(serverA);
    var serverB = new WebSocketBackend();
    var portB = Start(serverB);

    var client = Connect(portA, RolePlayer);
    WaitFor(() => serverA.ServerConnectionCount() == 1, "the client to join the first server");

    // Runs on the old receive thread after it stopped running and before it releases its connection, so
    // the reconnect installs the replacement ahead of the loop's compare-and-swap.
    var hookStarted = 0;
    var hookDone = 0;
    var reconnectResult = -1;
    client.ClientReceiveLoopExiting = () =>
    {
      if (Interlocked.Exchange(ref hookStarted, 1) != 0)
      {
        return;
      }

      try
      {
        Volatile.Write(ref reconnectResult, client.ClientConnect("127.0.0.1", portB, RolePlayer, ""));
      }
      finally
      {
        Volatile.Write(ref hookDone, 1);
      }
    };

    RunWithDeadline(serverA.ServerStop, "ServerStop of the first server");

    WaitFor(() => Volatile.Read(ref hookDone) == 1, "the reconnect inside the old loop's exit");
    WaitFor(() => serverB.ServerConnectionCount() == 1, "the client to join the second server");
    Assert.Equal(1, Volatile.Read(ref reconnectResult));

    // A loss reported here would be for the connection that was just replaced, not the live one.
    AssertStays(() => TransportRecorder.ClientDisconnects, 0, "client disconnect callbacks after the reconnect");

    TransportRecorder.ServerBroadcast(serverB, 21, new byte[] { 3 });
    WaitFor(() => TransportRecorder.ClientReceived.Count == 1, "the replacement connection to receive");
    Assert.Equal(21, TransportRecorder.ClientReceived.Single().Type);

    // The real loss still gets reported, once, for the replacement.
    client.ClientReceiveLoopExiting = null;
    RunWithDeadline(serverB.ServerStop, "ServerStop of the second server");

    WaitFor(() => TransportRecorder.ClientDisconnects == 1, "the replacement's loss to be reported");
    AssertStays(() => TransportRecorder.ClientDisconnects, 1, "client disconnect callbacks after the real loss");
  }
}
