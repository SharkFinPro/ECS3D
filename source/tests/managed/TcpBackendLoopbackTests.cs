using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using ECS3DNetTransport;
using Xunit;

namespace ECS3DManagedTests;

// Transport's callbacks are process-wide statics, so every test that observes them runs alone.
[CollectionDefinition("Transport", DisableParallelization = true)]
public class TransportCollection
{
}

// Drives a real TcpBackend over loopback sockets. Every wait polls a condition against a deadline so a
// regression fails with a message instead of hanging the run; the timeouts a test needs to see fire are
// shortened through the backend's internal fields.
[Collection("Transport")]
public sealed class TcpBackendLoopbackTests : IDisposable
{
  private const byte RolePlayer = 0;
  private const byte RoleEditor = 1;
  private const byte HandshakeType = 0xFF;
  private const int WaitMs = 5000;
  private const int MaxStartAttempts = 5;

  private readonly List<TcpBackend> _servers = new();
  private readonly List<TcpBackend> _clients = new();
  private readonly List<RawPeer> _peers = new();

  public TcpBackendLoopbackTests()
  {
    TransportRecorder.Register();
    TransportRecorder.Reset();
  }

  public void Dispose()
  {
    foreach (var client in _clients)
    {
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

  // A raw socket speaking the wire format by hand, so a test controls exactly when (and whether) it reads
  // or writes.
  private sealed class RawPeer : IDisposable
  {
    private readonly TcpClient _tcp;

    public NetworkStream Stream { get; }

    public RawPeer(int port, byte role, string token, int? receiveBufferBytes)
    {
      _tcp = new TcpClient { NoDelay = true };
      if (receiveBufferBytes.HasValue)
      {
        _tcp.ReceiveBufferSize = receiveBufferBytes.Value;
      }

      _tcp.Connect(IPAddress.Loopback, port);
      Stream = _tcp.GetStream();
      Stream.ReadTimeout = WaitMs;

      var payload = new[] { role }.Concat(Encoding.UTF8.GetBytes(token)).ToArray();
      Write(TcpBackend.FrameBytes(HandshakeType, payload));
    }

    public void Write(byte[] bytes, int offset = 0, int count = -1)
    {
      Stream.Write(bytes, offset, count < 0 ? bytes.Length - offset : count);
    }

    public void Dispose()
    {
      try { _tcp.Close(); } catch { /* ignore */ }
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

  private int Start(TcpBackend server, bool editMode = false, string token = "")
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

  private TcpBackend Connect(int port, byte role, string token = "")
  {
    var client = new TcpBackend();
    _clients.Add(client);
    Assert.Equal(1, client.ClientConnect("127.0.0.1", port, role, token));
    return client;
  }

  private RawPeer ConnectRaw(int port, int? receiveBufferBytes = null)
  {
    var peer = new RawPeer(port, RolePlayer, "", receiveBufferBytes);
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
  private int ConnectAndAwait(TcpBackend server, int port, int expectedCount)
  {
    Connect(port, RolePlayer);
    WaitFor(() => server.ServerConnectionCount() == expectedCount, $"connection {expectedCount} to join");
    return Authorized()[expectedCount - 1].ConnId;
  }

  [Fact]
  public void Handshake_ReportsTheGrantedRoleAndRefusesAWrongEditorToken()
  {
    var server = new TcpBackend();
    var port = Start(server, editMode: true, token: "secret");

    // The wrong token goes first so its refusal is settled before anything else is on the server.
    Connect(port, RoleEditor, "wrong");
    WaitFor(() => TransportRecorder.ServerDisconnected.Count == 1, "the refused editor to be dropped");
    var refusedId = TransportRecorder.ServerDisconnected.Single();
    WaitFor(() => TransportRecorder.ClientDisconnects == 1, "the refused client to be told");

    Connect(port, RoleEditor, "secret");
    Connect(port, RolePlayer);
    WaitFor(() => Authorized().Length == 2, "the two accepted connections to be authorized");
    WaitFor(() => server.ServerConnectionCount() == 2, "both accepted connections to be listed");

    var granted = Authorized();
    Assert.Equal(new[] { RolePlayer, RoleEditor }, granted.Select(a => a.Role).OrderBy(role => role).ToArray());
    Assert.DoesNotContain(granted, a => a.ConnId == refusedId);
    Assert.Equal(3, granted.Select(a => a.ConnId).Append(refusedId).Distinct().Count());
    Assert.Single(TransportRecorder.ServerDisconnected);
  }

  [Fact]
  public void ClientMessageArrivesWithItsConnectionId_AndBroadcastReachesEveryClient()
  {
    var server = new TcpBackend();
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

    var broadcast = new byte[] { 9, 8, 7 };
    TransportRecorder.ServerBroadcast(server, 6, broadcast);

    WaitFor(() => TransportRecorder.ClientReceived.Count == 1, "the client to receive the broadcast");
    var clientGot = TransportRecorder.ClientReceived.Single();
    Assert.Equal(6, clientGot.Type);
    Assert.Equal(broadcast, clientGot.Payload);

    Assert.True(TcpBackend.ReadFrame(viewer.Stream, out var rawType, out var rawPayload));
    Assert.Equal(6, rawType);
    Assert.Equal(broadcast, rawPayload);
  }

  [Fact]
  public void SendToMany_ReachesOnlyTheNamedConnection()
  {
    var server = new TcpBackend();
    var port = Start(server);

    Connect(port, RolePlayer);
    WaitFor(() => server.ServerConnectionCount() == 1, "the first client to join");
    var viewer = ConnectRaw(port);
    WaitFor(() => server.ServerConnectionCount() == 2, "the second client to join");
    var viewerId = Authorized()[1].ConnId;

    TransportRecorder.ServerSendToMany(server, new[] { viewerId }, 11, new byte[] { 1 });
    TransportRecorder.ServerBroadcast(server, 12, new byte[] { 2 });

    // Frames arrive in order on a stream, so the viewer seeing 11 then 12 proves the named send reached
    // it; the other client seeing only the broadcast proves it was left out of the named one.
    Assert.True(TcpBackend.ReadFrame(viewer.Stream, out var first, out _));
    Assert.True(TcpBackend.ReadFrame(viewer.Stream, out var second, out _));
    Assert.Equal(new byte[] { 11, 12 }, new[] { first, second });

    WaitFor(() => TransportRecorder.ClientReceived.Count >= 1, "the first client to receive the broadcast");
    AssertStays(() => TransportRecorder.ClientReceived.Count, 1, "messages the first client received");
    Assert.Equal(12, TransportRecorder.ClientReceived.Single().Type);
  }

  [Fact]
  public void StalledPeer_IsDroppedWhileTheOtherPeersKeepReceiving()
  {
    // 4000 ms leaves room for two healthy peers to drain a 4 MiB frame even on a loaded runner; the
    // stalled peer, last in the list, then spends the rest of the budget and is the only one dropped. A small server-side send
    // buffer stops platforms that auto-tune it large from absorbing the frame, and the loop is the
    // backstop: a peer that never reads can only be sent so many bytes before a send blocks.
    var server = new TcpBackend { SendTimeoutMs = 4000, AcceptedSendBufferBytes = 64 * 1024 };
    var port = Start(server);

    var healthyA = ConnectAndAwait(server, port, 1);
    var healthyB = ConnectAndAwait(server, port, 2);

    // A tiny receive buffer and no reads, so the frames below cannot fit in the kernel's buffers.
    ConnectRaw(port, receiveBufferBytes: 1024);
    WaitFor(() => server.ServerConnectionCount() == 3, "the stalled peer to join");
    var stalledId = Authorized()[2].ConnId;

    const int bigBytes = 4 * 1024 * 1024;
    const int maxBigFrames = 8;
    var bigFrames = 0;
    while (bigFrames < maxBigFrames && !TransportRecorder.ServerDisconnected.Contains(stalledId))
    {
      TransportRecorder.ServerBroadcast(server, 7, new byte[bigBytes]);
      ++bigFrames;
    }

    WaitFor(() => TransportRecorder.ServerDisconnected.Contains(stalledId), "the stalled peer to be dropped");
    WaitFor(() => TransportRecorder.ClientReceived.Count(m => m.Length == bigBytes) == 2 * bigFrames,
      "both healthy peers to receive every large frame");

    TransportRecorder.ServerBroadcast(server, 8, new byte[] { 1, 2, 3 });
    WaitFor(() => TransportRecorder.ClientReceived.Count(m => m.Type == 8) == 2,
      "both healthy peers to receive the next broadcast");

    Assert.DoesNotContain(healthyA, TransportRecorder.ServerDisconnected);
    Assert.DoesNotContain(healthyB, TransportRecorder.ServerDisconnected);
    Assert.Equal(2, server.ServerConnectionCount());
  }

  [Fact]
  public void FrameLargerThanOneReadBuffer_ArrivesIntactWhenWrittenInSmallChunks()
  {
    var server = new TcpBackend();
    var port = Start(server);
    var peer = ConnectRaw(port);
    WaitFor(() => server.ServerConnectionCount() == 1, "the peer to join");
    var peerId = Authorized()[0].ConnId;

    var payload = new byte[200_000];
    for (var i = 0; i < payload.Length; ++i)
    {
      payload[i] = (byte)(i * 31 % 251);
    }

    var frame = TcpBackend.FrameBytes(0x21, payload);
    const int chunk = 7000;
    for (var offset = 0; offset < frame.Length; offset += chunk)
    {
      peer.Write(frame, offset, Math.Min(chunk, frame.Length - offset));
      Thread.Sleep(1);
    }

    WaitFor(() => TransportRecorder.ServerReceived.Count == 1, "the whole frame to arrive");

    var received = TransportRecorder.ServerReceived.Single();
    Assert.Equal(peerId, received.ConnId);
    Assert.Equal(0x21, received.Type);
    Assert.Equal(payload, received.Payload);
  }

  [Fact]
  public void FrameThatStopsPartwayThroughItsBody_DropsOnlyThatConnection()
  {
    var server = new TcpBackend { BodyReadTimeoutMs = 300 };
    var port = Start(server);

    var healthy = Connect(port, RolePlayer);
    WaitFor(() => server.ServerConnectionCount() == 1, "the healthy client to join");
    var healthyId = Authorized()[0].ConnId;
    var stalled = ConnectRaw(port);
    WaitFor(() => server.ServerConnectionCount() == 2, "the stalling peer to join");
    var stalledId = Authorized()[1].ConnId;

    // Declares a 100000 byte body, then sends ten bytes of it and goes quiet.
    var header = new byte[4 + 1 + 10];
    BinaryPrimitives.WriteInt32BigEndian(header, 100_000);
    header[4] = 3;
    stalled.Write(header);

    WaitFor(() => TransportRecorder.ServerDisconnected.Contains(stalledId), "the stalled frame's connection to drop");
    Assert.True(TransportRecorder.LogContains("no body progress for 300 ms"));

    var message = new byte[] { 4, 5 };
    TransportRecorder.ClientSend(healthy, 4, message);
    WaitFor(() => TransportRecorder.ServerReceived.Count == 1, "the healthy client's message");

    var received = TransportRecorder.ServerReceived.Single();
    Assert.Equal(healthyId, received.ConnId);
    Assert.Equal(message, received.Payload);
    Assert.DoesNotContain(healthyId, TransportRecorder.ServerDisconnected);
    Assert.Equal(1, server.ServerConnectionCount());
  }

  [Fact]
  public void ClientConnect_FailsWhereNothingListens_AndRecoversAgainstARealServer()
  {
    var client = new TcpBackend();
    _clients.Add(client);

    Assert.Equal(0, client.ClientConnect("127.0.0.1", FreePort(), RolePlayer, ""));
    Assert.True(TransportRecorder.LogContains("Client failed to connect"));

    var server = new TcpBackend();
    var port = Start(server);
    Assert.Equal(1, client.ClientConnect("127.0.0.1", port, RolePlayer, ""));
    WaitFor(() => server.ServerConnectionCount() == 1, "the retried connection to join");
  }

  [Fact]
  public void ClientConnect_StaysConnectedToAListenerThatNeverAnswersTheHandshake()
  {
    // The connect completes at the TCP level, so a listener that accepts and says nothing neither fails
    // the connect nor ends the connection. The connect timeout cannot be provoked deterministically
    // over loopback.
    var silent = new TcpListener(IPAddress.Loopback, 0);
    silent.Start();
    try
    {
      var port = ((IPEndPoint)silent.LocalEndpoint).Port;
      var client = new TcpBackend();
      _clients.Add(client);

      Assert.Equal(1, client.ClientConnect("127.0.0.1", port, RolePlayer, ""));

      using var accepted = silent.AcceptTcpClient();
      AssertStays(() => TransportRecorder.ClientDisconnects, 0, "client disconnect callbacks while connected");

      RunWithDeadline(client.ClientDisconnect, "ClientDisconnect");
      WaitFor(() => TransportRecorder.ClientDisconnects == 1, "the disconnect callback after ClientDisconnect");
    }
    finally
    {
      silent.Stop();
    }
  }

  [Fact]
  public void ServerStop_ClosesConnectedClientsAndDeliversNothingAfterItReturns()
  {
    var server = new TcpBackend();
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
    var late = new TcpBackend();
    _clients.Add(late);
    Assert.Equal(0, late.ClientConnect("127.0.0.1", port, RolePlayer, ""));

    AssertStays(() => TransportRecorder.ServerReceived.Count, received, "messages received after ServerStop");
    AssertStays(() => TransportRecorder.ServerAuthorized.Count, authorized, "authorizations after ServerStop");
    AssertStays(() => TransportRecorder.ServerDisconnected.Count, disconnected, "disconnects after ServerStop");
  }

  [Fact]
  public void ClientDisconnect_ReturnsAndTheServerSeesTheConnectionGo()
  {
    var server = new TcpBackend();
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
    var server = new TcpBackend();
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
}
