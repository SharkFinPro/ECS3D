using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using ECS3DLauncher.Services;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherHostSessionTests
{
  private const string Folder = "projects/coin-rush";

  // Link-local on purpose; built from bytes because no documentation range covers it.
  private static readonly string LinkLocal = new IPAddress(new byte[] { 169, 254, 7, 7 }).ToString();

  private sealed class Rig
  {
    public Rig(TimeSpan window)
    {
      var engine = Path.GetFullPath("engine");
      var files = Enum.GetValues<EngineExecutable>()
        .Select(item => Path.Combine(engine, EngineExecutableNames.FileName(item, HostOs.Linux)))
        .ToArray();
      var location = new EngineLocator(new FakeFileSystem(files), HostOs.Linux, engine).Locate(engine);
      UserData = new UserDataDirectory(HostOs.Linux, name => name == "XDG_CONFIG_HOME" ? "cfg" : null);
      Session = new HostSession(
        new EngineLauncher(Runner, location),
        Probe,
        UserData,
        Clock,
        window,
        TimeSpan.FromMilliseconds(200),
        (span, _) =>
        {
          Clock.Advance(span);
          DelayLaunchCounts.Add(Runner.Launches.Count);
          OnDelay?.Invoke();
          return Task.CompletedTask;
        });
    }

    public RecordingProcessRunner Runner { get; } = new();

    public FakePortProbe Probe { get; } = new();

    public FakeClock Clock { get; } = new();

    public UserDataDirectory UserData { get; }

    public HostSession Session { get; }

    public List<int> DelayLaunchCounts { get; } = [];

    public Action? OnDelay { get; set; }

    // The server starts listening during the first wait.
    public Rig ListenAfterFirstWait()
    {
      OnDelay = () => Probe.Accepts = true;
      return this;
    }

    public Task<HostStartResult> StartAsync(int port, CancellationToken cancellationToken) =>
      Session.StartAsync(Folder, "Coin Rush", port, cancellationToken);

    public Task<HostStartResult> StartAsync() => StartAsync(3001, CancellationToken.None);
  }

  private static Rig NewRig() => new(TimeSpan.FromSeconds(10));

  [Fact]
  public async Task ServerAndClientGetTheirArgumentListsAndTheClientWaitsForTheProbe()
  {
    var rig = NewRig().ListenAfterFirstWait();

    var result = await rig.StartAsync();

    Assert.Equal(HostStartKind.Started, result.Kind);
    Assert.Equal(2, rig.Runner.Launches.Count);
    var server = rig.Runner.Launches[0];
    Assert.Equal(new[] { "--project", Folder, "--port", "3001" }, server.Arguments);
    Assert.True(server.HideWindow);
    var client = rig.Runner.Launches[1];
    Assert.Equal(new[] { "--host", "127.0.0.1", "--port", "3001" }, client.Arguments);
    Assert.False(client.HideWindow);
    Assert.Contains("ECS3DClient", client.FileName);
    Assert.NotEmpty(rig.DelayLaunchCounts);
    Assert.All(rig.DelayLaunchCounts, count => Assert.Equal(1, count));
  }

  [Fact]
  public async Task PortThatAlreadyAnswersFailsWithoutStartingAServer()
  {
    var rig = NewRig();
    rig.Probe.Accepts = true;

    var result = await rig.StartAsync();

    Assert.Equal(HostStartKind.PortInUse, result.Kind);
    Assert.Contains("3001 is already in use", result.Message);
    Assert.Contains("Settings", result.Message);
    Assert.Empty(rig.Runner.Launches);
    Assert.False(rig.Session.IsRunning);
  }

  [Fact]
  public async Task OnlyOneSessionRunsAtATime()
  {
    var rig = NewRig().ListenAfterFirstWait();
    await rig.StartAsync();
    var probeCalls = rig.Probe.Calls;

    var second = await rig.StartAsync(3002, CancellationToken.None);

    Assert.Equal(HostStartKind.AlreadyRunning, second.Kind);
    Assert.False(second.Succeeded);
    Assert.Equal(2, rig.Runner.Launches.Count);
    Assert.Equal(probeCalls, rig.Probe.Calls);
    Assert.False(rig.Runner.Processes[0].Killed);
    Assert.True(rig.Session.IsRunning);
    Assert.Equal(3001, rig.Session.Port);
  }

  [Fact]
  public async Task ServerExitingDuringStartupFailsWithCodeAndLogAndNoClient()
  {
    var rig = NewRig();
    rig.OnDelay = () => rig.Runner.Processes[0].Exit(7);

    var result = await rig.StartAsync();

    var log = rig.UserData.LogFile(EngineExecutable.Server);
    Assert.Equal(HostStartKind.ServerExited, result.Kind);
    Assert.Contains("code 7", result.Message);
    Assert.Contains(log, result.Message);
    Assert.Equal(log, result.LogPath);
    Assert.Single(rig.Runner.Launches);
    Assert.False(rig.Session.IsRunning);
  }

  [Fact]
  public async Task StartupWindowRunningOutKillsTheServer()
  {
    var rig = new Rig(TimeSpan.FromSeconds(1));

    var result = await rig.StartAsync();

    Assert.Equal(HostStartKind.TimedOut, result.Kind);
    Assert.Contains("within 1 second.", result.Message);
    Assert.True(rig.Runner.Processes[0].Killed);
    Assert.Single(rig.Runner.Launches);
    Assert.False(rig.Session.IsRunning);
    Assert.Equal(5, rig.DelayLaunchCounts.Count);
  }

  [Fact]
  public async Task CancellingTheStartKillsTheServer()
  {
    var rig = NewRig();
    using var cancel = new CancellationTokenSource();
    rig.OnDelay = cancel.Cancel;

    var result = await rig.StartAsync(3001, cancel.Token);

    Assert.Equal(HostStartKind.Cancelled, result.Kind);
    Assert.True(rig.Runner.Processes[0].Killed);
    Assert.Single(rig.Runner.Launches);
    Assert.False(rig.Session.IsRunning);
  }

  [Fact]
  public async Task CancellingDuringThePortCheckReturnsCancelled()
  {
    var rig = NewRig();
    using var cancel = new CancellationTokenSource();
    await cancel.CancelAsync();

    var result = await rig.StartAsync(3001, cancel.Token);

    Assert.Equal(HostStartKind.Cancelled, result.Kind);
    Assert.Empty(rig.Runner.Launches);
    Assert.False(rig.Session.IsStarting);
  }

  [Fact]
  public async Task StopDuringStartupCancelsTheStartAndKillsTheServer()
  {
    var rig = NewRig();
    var starting = false;
    var stopped = false;
    rig.OnDelay = () =>
    {
      starting = rig.Session.IsStarting;
      stopped = rig.Session.Stop();
    };

    var result = await rig.StartAsync();

    Assert.True(starting);
    Assert.True(stopped);
    Assert.Equal(HostStartKind.Cancelled, result.Kind);
    Assert.True(rig.Runner.Processes[0].Killed);
    Assert.False(rig.Session.IsStarting);
    Assert.False(rig.Session.IsRunning);
  }

  [Fact]
  public async Task AProbeFailureKillsTheServerAndPropagates()
  {
    var rig = NewRig();
    rig.OnDelay = () => rig.Probe.Failure = new InvalidOperationException("probe broke");

    await Assert.ThrowsAsync<InvalidOperationException>(() => rig.StartAsync());

    Assert.True(rig.Runner.Processes[0].Killed);
    Assert.False(rig.Session.IsStarting);
    Assert.False(rig.Session.IsRunning);
  }

  [Fact]
  public async Task ServerDyingWhileTheHostWindowOpensIsReportedAsExited()
  {
    var rig = NewRig().ListenAfterFirstWait();
    rig.Runner.FailFor = launch =>
    {
      if (launch.FileName.Contains("ECS3DClient"))
      {
        rig.Runner.Processes[0].Exit(9);
      }

      return null;
    };

    var result = await rig.StartAsync();

    Assert.Equal(HostStartKind.ServerExited, result.Kind);
    Assert.Contains("code 9", result.Message);
    Assert.False(rig.Session.IsRunning);
  }

  [Fact]
  public async Task ServerFailingToLaunchIsReported()
  {
    var rig = NewRig();
    rig.Runner.FailFor = launch => launch.FileName.Contains("ECS3DServer") ? "no permission" : null;

    var result = await rig.StartAsync();

    Assert.Equal(HostStartKind.LaunchFailed, result.Kind);
    Assert.Contains("no permission", result.Message);
    Assert.False(rig.Session.IsRunning);
  }

  [Fact]
  public async Task ClientFailingToLaunchKeepsTheServerRunning()
  {
    var rig = NewRig().ListenAfterFirstWait();
    rig.Runner.FailFor = launch => launch.FileName.Contains("ECS3DClient") ? "no display" : null;

    var result = await rig.StartAsync();

    Assert.Equal(HostStartKind.StartedWithoutWindow, result.Kind);
    Assert.True(result.Succeeded);
    Assert.Contains("no display", result.Message);
    Assert.True(rig.Session.IsRunning);
    Assert.False(rig.Runner.Processes[0].Killed);
  }

  [Fact]
  public async Task ServerExitWhileRunningRaisesEndedWithCodeAndLog()
  {
    var rig = NewRig().ListenAfterFirstWait();
    var ended = new TaskCompletionSource<HostSessionEnd>();
    rig.Session.Ended += end => ended.TrySetResult(end);
    await rig.StartAsync();

    rig.Runner.Processes[0].Exit(3);

    var end = await ended.Task.WaitAsync(TimeSpan.FromSeconds(5));
    Assert.Equal(3, end.ExitCode);
    Assert.Equal(rig.UserData.LogFile(EngineExecutable.Server), end.LogPath);
    Assert.False(rig.Session.IsRunning);
  }

  [Fact]
  public async Task StopKillsTheServerWithoutReportingACrash()
  {
    var rig = NewRig().ListenAfterFirstWait();
    var endedCount = 0;
    rig.Session.Ended += _ => Interlocked.Increment(ref endedCount);
    await rig.StartAsync();

    var stopped = rig.Session.Stop();
    // The fake's continuations run inline, so a watch still registered would raise Ended inside Exit.
    rig.Runner.Processes[0].Exit(1);

    Assert.True(stopped);
    Assert.True(rig.Runner.Processes[0].Killed);
    Assert.False(rig.Session.IsRunning);
    Assert.Equal(0, endedCount);
    Assert.False(rig.Session.Stop());
  }

  [Fact]
  public async Task UptimeComesFromTheClock()
  {
    var rig = NewRig().ListenAfterFirstWait();
    Assert.Equal(TimeSpan.Zero, rig.Session.Uptime);
    await rig.StartAsync();
    Assert.Equal("Coin Rush", rig.Session.DisplayName);

    rig.Clock.Advance(TimeSpan.FromSeconds(90));

    Assert.Equal(TimeSpan.FromSeconds(90), rig.Session.Uptime);
    rig.Session.Stop();
    Assert.Equal(TimeSpan.Zero, rig.Session.Uptime);
    Assert.Null(rig.Session.Port);
  }

  [Fact]
  public async Task OpenAnotherWindowJoinsLocallyOnlyWhileHosting()
  {
    var rig = NewRig().ListenAfterFirstWait();

    Assert.NotNull(rig.Session.OpenAnotherWindow());
    Assert.Empty(rig.Runner.Launches);

    await rig.StartAsync();
    var extra = rig.Session.OpenAnotherWindow();

    Assert.Null(extra);
    Assert.Equal(3, rig.Runner.Launches.Count);
    Assert.Equal(new[] { "--host", "127.0.0.1", "--port", "3001" }, rig.Runner.Launches[2].Arguments);
  }

  private static NetworkInterfaceInfo Nic(string name, NetworkInterfaceKind kind, bool up, params string[] addresses) =>
    new(name, kind, up, addresses.Select(IPAddress.Parse).ToList());

  [Fact]
  public void LanAddressesKeepOnlyUsableIpv4OnInterfacesThatAreUp()
  {
    var found = LanAddresses.From(
    [
      Nic("wlan0", NetworkInterfaceKind.Wireless, true, "192.0.2.24", "2001:db8::2", LinkLocal),
      Nic("eth0", NetworkInterfaceKind.Ethernet, true, "198.51.100.5"),
      Nic("eth1", NetworkInterfaceKind.Ethernet, false, "198.51.100.99"),
      Nic("lo", NetworkInterfaceKind.Loopback, true, "127.0.0.1"),
      Nic("tun0", NetworkInterfaceKind.Other, true, "127.0.1.1", "203.0.113.2"),
      Nic("v6only", NetworkInterfaceKind.Ethernet, true, "2001:db8::1"),
    ]);

    Assert.Equal(
      new[] { "Ethernet 198.51.100.5", "Wi-Fi 192.0.2.24", "tun0 203.0.113.2" },
      found.Select(item => $"{item.Label} {item.Address}").ToArray());
  }

  [Fact]
  public void InterfacesSharingALabelAreToldApartByName()
  {
    var found = LanAddresses.From(
    [
      Nic("eth0", NetworkInterfaceKind.Ethernet, true, "198.51.100.5"),
      Nic("eth1", NetworkInterfaceKind.Ethernet, true, "203.0.113.5"),
      Nic("wlan0", NetworkInterfaceKind.Wireless, true, "192.0.2.5"),
    ]);

    Assert.Equal(
      new[] { "Ethernet (eth0)", "Ethernet (eth1)", "Wi-Fi" },
      found.Select(item => item.Label).ToArray());
  }

  [Fact]
  public void LanAddressesOrderByLabelThenAddress()
  {
    var found = LanAddresses.From(
    [
      Nic("b", NetworkInterfaceKind.Other, true, "198.51.100.9", "198.51.100.10"),
      Nic("a", NetworkInterfaceKind.Other, true, "192.0.2.2"),
    ]);

    Assert.Equal(
      new[] { "192.0.2.2", "198.51.100.10", "198.51.100.9" },
      found.Select(item => item.Address).ToArray());
  }

  [Theory]
  [InlineData(" 192.0.2.24 ", "", "192.0.2.24", 3000)]
  [InlineData("192.0.2.24", "3001", "192.0.2.24", 3001)]
  [InlineData("host.local", " 65535 ", "host.local", 65535)]
  [InlineData("host.local", "1", "host.local", 1)]
  public void JoinTargetAcceptsValidInput(string address, string port, string expectedAddress, int expectedPort)
  {
    var result = JoinTarget.Parse(address, port, 3000);

    Assert.True(result.IsValid);
    Assert.Equal(new JoinTarget(expectedAddress, expectedPort), result.Target);
  }

  [Theory]
  [InlineData("host:", "3000", JoinTarget.InvalidAddressError)]
  [InlineData(":3000", "3000", JoinTarget.InvalidAddressError)]
  [InlineData("-host", "3000", JoinTarget.InvalidAddressError)]
  [InlineData("--port", "", JoinTarget.InvalidAddressError)]
  [InlineData("", "3000", JoinTarget.AddressError)]
  [InlineData("   ", "3000", JoinTarget.AddressError)]
  [InlineData("host", "0", JoinTarget.PortError)]
  [InlineData("host", "65536", JoinTarget.PortError)]
  [InlineData("host", "-1", JoinTarget.PortError)]
  [InlineData("host", "30.5", JoinTarget.PortError)]
  [InlineData("host", "abc", JoinTarget.PortError)]
  public void JoinTargetRejectsInvalidInput(string address, string port, string error)
  {
    var result = JoinTarget.Parse(address, port, 3000);

    Assert.False(result.IsValid);
    Assert.Equal(error, result.Error);
  }

  [Theory]
  [InlineData("192.0.2.24:3001", "192.0.2.24", "3001")]
  [InlineData("host.local:70000", "host.local", "70000")]
  [InlineData("  host.local:3001 ", "host.local", "3001")]
  [InlineData("[::1]:3000", "::1", "3000")]
  [InlineData("192.0.2.24", "192.0.2.24", null)]
  [InlineData("::1", "::1", null)]
  [InlineData("host:", "host:", null)]
  [InlineData(":3000", ":3000", null)]
  public void PastedAddressSplitsOnlyWhenItCarriesAPort(string pasted, string address, string? port)
  {
    Assert.Equal((address, port), JoinTarget.SplitPasted(pasted));
  }

  [Fact]
  public void BracketedAddressLosesItsBracketsEverywhere()
  {
    Assert.Equal(("::1", (string?)null), JoinTarget.SplitPasted("[::1]"));
    Assert.Equal("::1", JoinTarget.Parse("[::1]", "3000", 3000).Target?.Address);
  }

  [Fact]
  public void PastedOutOfRangePortIsRejectedByParse()
  {
    var (address, port) = JoinTarget.SplitPasted("host.local:70000");

    Assert.False(JoinTarget.Parse(address, port, 3000).IsValid);
  }

  [Fact]
  public async Task TcpPortProbeSeesAListenerAndNotAClosedPort()
  {
    var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    var port = ((IPEndPoint)listener.LocalEndpoint).Port;
    var probe = new TcpPortProbe(TimeSpan.FromSeconds(2));
    try
    {
      Assert.True(await probe.AcceptsConnectionAsync("127.0.0.1", port, CancellationToken.None));
    }
    finally
    {
      listener.Stop();
    }

    Assert.False(await probe.AcceptsConnectionAsync("127.0.0.1", port, CancellationToken.None));
  }
}
