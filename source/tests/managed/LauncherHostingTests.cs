using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Threading.Tasks;
using ECS3DLauncher.Services;
using ECS3DLauncher.Shared.Models;
using ECS3DLauncher.Shell;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherHostingTests
{
  private static readonly Project Alpha = new() { Name = "Alpha", Path = "/work/alpha", Edited = "today" };
  private static readonly Project Beta = new() { Name = "Beta", Path = "/work/beta", Edited = "today" };

  private sealed class FakeNetworkInfo : INetworkInfo
  {
    public List<NetworkInterfaceInfo> Interfaces { get; } = [];

    public IReadOnlyList<NetworkInterfaceInfo> GetInterfaces() => Interfaces;
  }

  private sealed class Rig
  {
    private readonly LauncherRig _launcher = new();

    public Rig(params EngineExecutable[] missing)
    {
      _launcher.AddEngine(LauncherRig.LauncherDir, missing);
      Network.Interfaces.Add(new NetworkInterfaceInfo(
        "wlan0", NetworkInterfaceKind.Wireless, true, [IPAddress.Parse("192.168.1.24")]));
      Main = NewMain();
    }

    public RecordingProcessRunner Runner { get; } = new();

    public FakePortProbe Probe { get; } = new();

    public FakeClock Clock { get; } = new();

    public FakeNetworkInfo Network { get; } = new();

    public MainWindowViewModel Main { get; }

    public HostingViewModel Hosting => Main.Hosting;

    // The server starts listening during the first wait.
    public Rig ListenAfterFirstWait()
    {
      OnDelay = () => Probe.Accepts = true;
      return this;
    }

    public Action? OnDelay { get; set; }

    public string? Toast => Main.ToastMessage;

    private MainWindowViewModel NewMain()
    {
      var services = new HostingServices(
        Network,
        launcher => new HostSession(
          launcher,
          Probe,
          ShellFixtures.UserData,
          Clock,
          TimeSpan.FromSeconds(10),
          TimeSpan.FromMilliseconds(200),
          (span, _) =>
          {
            Clock.Advance(span);
            OnDelay?.Invoke();
            return Task.CompletedTask;
          }));
      return new MainWindowViewModel(
        _launcher.NewEditor(),
        _launcher.NewRecents(),
        Runner,
        new EarlyExitWatcher(ShellFixtures.UserData, TimeSpan.FromHours(1)),
        _launcher.NewCatalog(),
        _ => null,
        "1.2.3",
        action => action(),
        () => _launcher.Time.Now,
        _launcher.Files,
        action => action(),
        services);
    }

    public void Host(Project project) => Main.HostProjectCommand.Execute(project);
  }

  [Fact]
  public void HostingIsOfferedForARealPresentProjectWhenTheEngineIsComplete()
  {
    var rig = new Rig();
    var missing = new Project { Name = "Gone", Path = "/work/gone", Edited = "", IsMissing = true };

    Assert.True(rig.Main.HostProjectCommand.CanExecute(Alpha));
    Assert.False(rig.Main.HostProjectCommand.CanExecute(missing));
    Assert.Null(rig.Main.Projects.HostTooltip);
  }

  [Theory]
  [InlineData(EngineExecutable.Server)]
  [InlineData(EngineExecutable.Client)]
  public void HostingIsDisabledWithATooltipWhenTheServerOrTheClientIsMissing(EngineExecutable gone)
  {
    var rig = new Rig(gone);

    Assert.False(rig.Main.HostProjectCommand.CanExecute(Alpha));
    Assert.Contains(EngineExecutableNames.FileName(gone, HostOs.Linux), rig.Main.Projects.HostTooltip);

    rig.Host(Alpha);

    Assert.Empty(rig.Runner.Launches);
    Assert.False(rig.Hosting.IsHosting);
  }

  [Fact]
  public void AStartedGameOpensThePanelAndTheSidebarPill()
  {
    var rig = new Rig().ListenAfterFirstWait();

    rig.Host(Alpha);

    Assert.Equal(new[] { "--project", "/work/alpha", "--port", "3000" }, rig.Runner.Launches[0].Arguments);
    Assert.True(rig.Hosting.IsHosting);
    Assert.True(rig.Hosting.IsPanelOpen);
    Assert.Equal("Hosting Alpha", rig.Hosting.PillText);
    Assert.Equal("192.168.1.24 : 3000", rig.Hosting.PillAddress);
    Assert.Equal("Hosting Alpha", rig.Hosting.Panel!.Title);
    Assert.False(rig.Hosting.IsStarting);
    Assert.Null(rig.Toast);
  }

  [Fact]
  public void WithoutAnyNetworkAddressThePillShowsTheLoopbackAddress()
  {
    var rig = new Rig().ListenAfterFirstWait();
    rig.Network.Interfaces.Clear();

    rig.Host(Alpha);

    Assert.Equal("127.0.0.1 : 3000", rig.Hosting.PillAddress);
    Assert.True(rig.Hosting.Panel!.HasNoAddresses);
  }

  [Fact]
  public void TheStartingLineShowsWhileTheServerIsStarting()
  {
    var rig = new Rig();
    string? seen = null;
    rig.OnDelay = () =>
    {
      seen = rig.Hosting.StartingText;
      rig.Probe.Accepts = true;
    };

    rig.Host(Alpha);

    Assert.Equal("Starting Alpha...", seen);
    Assert.False(rig.Hosting.IsStarting);
    Assert.Equal("", rig.Hosting.StartingText);
  }

  [Fact]
  public void APortThatIsAlreadyInUseToastsTheHint()
  {
    var rig = new Rig();
    rig.Probe.Accepts = true;

    rig.Host(Alpha);

    Assert.Contains("already in use", rig.Toast);
    Assert.Contains("Settings", rig.Toast);
    Assert.False(rig.Hosting.IsHosting);
    Assert.False(rig.Hosting.IsPanelOpen);
  }

  [Fact]
  public void AServerThatExitsWhileStartingToastsTheCodeAndOffersTheLog()
  {
    var rig = new Rig();
    rig.OnDelay = () => rig.Runner.Processes[0].Exit(2);

    rig.Host(Alpha);

    Assert.Contains("code 2", rig.Toast);
    Assert.Equal(ShellFixtures.ExpectedLog("server"), rig.Main.ToastLogPath);
    Assert.False(rig.Hosting.IsHosting);
  }

  [Fact]
  public void AServerThatCannotBeLaunchedToastsTheError()
  {
    var rig = new Rig();
    rig.Runner.FailFor = _ => "cannot start";

    rig.Host(Alpha);

    Assert.Equal("cannot start", rig.Toast);
    Assert.Null(rig.Main.ToastLogPath);
  }

  [Fact]
  public void AHostWindowThatFailsToOpenKeepsTheGameAndToasts()
  {
    var rig = new Rig().ListenAfterFirstWait();
    rig.Runner.FailFor = launch => launch.FileName.Contains("Client") ? "no window" : null;

    rig.Host(Alpha);

    Assert.True(rig.Hosting.IsHosting);
    Assert.Equal("no window", rig.Toast);
  }

  [Fact]
  public void TheServerExitingOnItsOwnClosesThePanelAndToastsTheCodeAndTheLog()
  {
    var rig = new Rig().ListenAfterFirstWait();
    rig.Host(Alpha);

    rig.Runner.Processes[0].Exit(7);

    Assert.False(rig.Hosting.IsHosting);
    Assert.False(rig.Hosting.IsPanelOpen);
    Assert.Equal("", rig.Hosting.PillText);
    Assert.Contains("code 7", rig.Toast);
    Assert.Contains("port 3000", rig.Toast);
    Assert.Equal(ShellFixtures.ExpectedLog("server"), rig.Main.ToastLogPath);
  }

  [Fact]
  public void HostingASecondProjectAsksFirstAndCancelKeepsTheRunningGame()
  {
    var rig = new Rig().ListenAfterFirstWait();
    rig.Host(Alpha);

    rig.Host(Beta);

    Assert.True(rig.Main.HasPrompt);
    Assert.Equal("Stop the current game and host Beta?", rig.Main.Prompt!.Message);
    rig.Main.Prompt.CancelCommand.Execute(null);
    Assert.False(rig.Main.HasPrompt);
    Assert.Equal("Hosting Alpha", rig.Hosting.PillText);
    Assert.Single(rig.Runner.Launches, launch => launch.Arguments.Contains("--project"));
    Assert.False(rig.Runner.Processes[0].Killed);
  }

  [Fact]
  public void ConfirmingStopsTheRunningGameAndHostsTheNewOne()
  {
    var rig = new Rig().ListenAfterFirstWait();
    rig.Host(Alpha);
    rig.Host(Beta);
    rig.Probe.Accepts = false;

    rig.Main.Prompt!.ConfirmCommand.Execute(null);

    Assert.False(rig.Main.HasPrompt);
    Assert.True(rig.Runner.Processes[0].Killed);
    Assert.Equal("Hosting Beta", rig.Hosting.PillText);
    Assert.Contains("/work/beta", rig.Runner.Launches.Select(launch => launch.Arguments).Last(
      arguments => arguments.Contains("--project")));
    Assert.Null(rig.Toast);
  }

  [Fact]
  public void AddressRowsListEveryShareableAddressWithTheirPort()
  {
    var rig = new Rig().ListenAfterFirstWait();
    rig.Network.Interfaces.Add(new NetworkInterfaceInfo(
      "eth0", NetworkInterfaceKind.Ethernet, true, [IPAddress.Parse("10.0.0.5")]));
    rig.Network.Interfaces.Add(new NetworkInterfaceInfo(
      "lo", NetworkInterfaceKind.Loopback, true, [IPAddress.Loopback]));

    rig.Host(Alpha);

    var rows = rig.Hosting.Panel!.Addresses;
    Assert.Equal(new[] { "Ethernet", "Wi-Fi" }, rows.Select(row => row.Label));
    Assert.Equal(new[] { "10.0.0.5 : 3000", "192.168.1.24 : 3000" }, rows.Select(row => row.Text));
    Assert.Equal("Port 3000. Change it in Settings.", rig.Hosting.Panel.PortText);
  }

  [Fact]
  public void CopyPutsTheAddressAndPortOnTheClipboard()
  {
    var rig = new Rig().ListenAfterFirstWait();
    var copied = new List<string>();
    rig.Hosting.CopyToClipboard = text =>
    {
      copied.Add(text);
      return Task.FromResult(true);
    };
    rig.Host(Alpha);

    rig.Hosting.Panel!.Addresses[0].CopyCommand.Execute(null);

    Assert.Equal(new[] { "192.168.1.24:3000" }, copied);
    Assert.Null(rig.Toast);
  }

  [Fact]
  public void ACopyThatFailsToasts()
  {
    var rig = new Rig().ListenAfterFirstWait();
    rig.Host(Alpha);

    rig.Hosting.Panel!.Addresses[0].CopyCommand.Execute(null);

    Assert.Equal("Could not copy 192.168.1.24:3000", rig.Toast);
  }

  [Fact]
  public void StopServerKillsItAndRemovesThePanelAndThePill()
  {
    var rig = new Rig().ListenAfterFirstWait();
    rig.Host(Alpha);

    rig.Hosting.Panel!.StopCommand.Execute(null);

    Assert.True(rig.Runner.Processes[0].Killed);
    Assert.False(rig.Hosting.IsHosting);
    Assert.False(rig.Hosting.IsPanelOpen);
    Assert.Null(rig.Toast);
  }

  [Fact]
  public void HideClosesThePanelButKeepsThePillWhichReopensIt()
  {
    var rig = new Rig().ListenAfterFirstWait();
    rig.Host(Alpha);

    rig.Hosting.Panel!.HideCommand.Execute(null);

    Assert.False(rig.Hosting.IsPanelOpen);
    Assert.True(rig.Hosting.IsHosting);
    rig.Hosting.ShowPanelCommand.Execute(null);
    Assert.True(rig.Hosting.IsPanelOpen);
  }

  [Fact]
  public void ThePanelPointsToSettings()
  {
    var rig = new Rig().ListenAfterFirstWait();
    rig.Host(Alpha);

    rig.Hosting.Panel!.OpenSettingsCommand.Execute(null);

    Assert.Equal("settings", rig.Main.ActiveNav);
    Assert.False(rig.Hosting.IsPanelOpen);
    Assert.True(rig.Hosting.IsHosting);
  }

  [Fact]
  public void OpenAnotherWindowStartsAClientOnTheHostedPort()
  {
    var rig = new Rig().ListenAfterFirstWait();
    rig.Host(Alpha);
    var before = rig.Runner.Launches.Count;

    rig.Hosting.Panel!.OpenWindowCommand.Execute(null);

    Assert.Equal(before + 1, rig.Runner.Launches.Count);
    Assert.Equal(new[] { "--host", "127.0.0.1", "--port", "3000" }, rig.Runner.Launches[^1].Arguments);
    Assert.Null(rig.Toast);
  }

  [Fact]
  public void OpenAnotherWindowToastsWhenTheClientCannotStart()
  {
    var rig = new Rig().ListenAfterFirstWait();
    rig.Host(Alpha);
    rig.Runner.FailFor = _ => "no client";

    rig.Hosting.Panel!.OpenWindowCommand.Execute(null);

    Assert.Equal("no client", rig.Toast);
  }

  [Fact]
  public void TickRereadsTheRunningTime()
  {
    var rig = new Rig().ListenAfterFirstWait();
    rig.Host(Alpha);
    var panel = rig.Hosting.Panel!;
    var before = panel.RunningText;

    rig.Clock.Advance(TimeSpan.FromSeconds(125));
    rig.Hosting.Tick();

    Assert.Equal("Running for 2m 05s", panel.RunningText);
    Assert.NotEqual(before, panel.RunningText);
  }

  [Theory]
  [InlineData(0, "Running for 0s")]
  [InlineData(59, "Running for 59s")]
  [InlineData(60, "Running for 1m 00s")]
  [InlineData(3723, "Running for 1h 02m 03s")]
  [InlineData(-5, "Running for 0s")]
  public void RunningTimeIsFormattedInTheLargestUnits(int seconds, string expected) =>
    Assert.Equal(expected, HostPanelViewModel.RunningFor(TimeSpan.FromSeconds(seconds)));

  [Fact]
  public void AGameCanBeHostedAgainAfterItWasStopped()
  {
    var rig = new Rig().ListenAfterFirstWait();
    rig.Host(Alpha);
    rig.Hosting.Panel!.StopCommand.Execute(null);
    rig.Probe.Accepts = false;

    rig.Host(Beta);

    Assert.Equal("Hosting Beta", rig.Hosting.PillText);
  }
}
