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

  private static readonly IPAddress Wifi = new(new byte[] { 192, 168, 1, 24 });
  private static readonly IPAddress Wired = new(new byte[] { 10, 0, 0, 5 });

  private sealed class FakeNetworkInfo : INetworkInfo
  {
    public List<NetworkInterfaceInfo> Interfaces { get; } = [];

    public bool Throws { get; set; }

    public IReadOnlyList<NetworkInterfaceInfo> GetInterfaces() =>
      Throws ? throw new InvalidOperationException("no interfaces") : Interfaces;
  }

  private sealed class Rig
  {
    private readonly LauncherRig _launcher = new();

    public Rig(params EngineExecutable[] missing)
    {
      _launcher.AddEngine(LauncherRig.LauncherDir, missing);
      Network.Interfaces.Add(new NetworkInterfaceInfo(
        "wlan0", NetworkInterfaceKind.Wireless, true, [Wifi]));
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

    public List<Action> Deferred { get; } = [];

    public bool Defer { get; set; }

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
        action =>
        {
          if (Defer)
          {
            Deferred.Add(action);
          }
          else
          {
            action();
          }
        },
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
    Assert.DoesNotContain("port", rig.Toast);
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
      "eth0", NetworkInterfaceKind.Ethernet, true, [Wired]));
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

  private static void ExitLater(Rig rig, int code)
  {
    rig.Defer = true;
    rig.Runner.Processes[0].Exit(code);
    rig.Defer = false;
  }

  [Fact]
  public void AnEndedEventThatArrivesAfterStopIsDropped()
  {
    var rig = new Rig().ListenAfterFirstWait();
    rig.Host(Alpha);
    ExitLater(rig, 7);
    Assert.Single(rig.Deferred);

    rig.Hosting.Panel!.StopCommand.Execute(null);
    rig.Deferred.ForEach(action => action());

    Assert.Null(rig.Toast);
    Assert.False(rig.Hosting.IsHosting);
  }

  [Fact]
  public void AnEndedEventFromAReplacedSessionLeavesTheNewGameAlone()
  {
    var rig = new Rig().ListenAfterFirstWait();
    rig.Host(Alpha);
    ExitLater(rig, 7);
    rig.Probe.Accepts = false;
    rig.Host(Beta);

    rig.Deferred.ForEach(action => action());

    Assert.Equal("Hosting Beta", rig.Hosting.PillText);
    Assert.True(rig.Hosting.IsPanelOpen);
    Assert.Null(rig.Toast);
  }

  [Fact]
  public void HostingWhileAStartIsInProgressIsIgnored()
  {
    var rig = new Rig();
    rig.OnDelay = () =>
    {
      rig.Host(Beta);
      rig.Probe.Accepts = true;
    };

    rig.Host(Alpha);

    Assert.False(rig.Main.HasPrompt);
    Assert.Equal("Hosting Alpha", rig.Hosting.PillText);
    Assert.Single(rig.Runner.Launches, launch => launch.Arguments.Contains("--project"));
  }

  [Fact]
  public void AnInterfaceListThatCannotBeReadStillOpensThePanelWithoutAddresses()
  {
    var rig = new Rig().ListenAfterFirstWait();
    rig.Network.Throws = true;

    rig.Host(Alpha);

    Assert.True(rig.Hosting.IsPanelOpen);
    Assert.True(rig.Hosting.Panel!.HasNoAddresses);
    Assert.Equal("127.0.0.1 : 3000", rig.Hosting.PillAddress);
    Assert.Null(rig.Toast);
  }

  [Fact]
  public void AProbeThatThrowsToastsAndALaterHostStillWorks()
  {
    var rig = new Rig();
    rig.Probe.Failure = new InvalidOperationException("probe broke");

    rig.Host(Alpha);

    Assert.Equal("probe broke", rig.Toast);
    Assert.False(rig.Hosting.IsStarting);
    rig.Probe.Failure = null;
    rig.ListenAfterFirstWait();
    rig.Host(Beta);
    Assert.Equal("Hosting Beta", rig.Hosting.PillText);
  }

  [Fact]
  public void EscapeCancelsAQuestionFirstAndThenHidesThePanel()
  {
    var rig = new Rig().ListenAfterFirstWait();
    rig.Host(Alpha);
    Assert.False(rig.Main.IsBackgroundEnabled);
    rig.Host(Beta);
    Assert.True(rig.Main.HasPrompt);

    rig.Main.CloseOverlayCommand.Execute(null);
    Assert.False(rig.Main.HasPrompt);
    Assert.True(rig.Hosting.IsPanelOpen);

    rig.Main.CloseOverlayCommand.Execute(null);
    Assert.False(rig.Hosting.IsPanelOpen);
    Assert.True(rig.Main.IsBackgroundEnabled);
    Assert.True(rig.Hosting.IsHosting);
  }

  [Fact]
  public void EscapeClosesTheJoinDialogAndTheWindowBehindItIsDisabledWhileItIsOpen()
  {
    var rig = new Rig();

    rig.Main.OpenJoinCommand.Execute(null);
    Assert.NotNull(rig.Main.JoinDialog);
    Assert.False(rig.Main.IsBackgroundEnabled);

    rig.Main.CloseOverlayCommand.Execute(null);

    Assert.Null(rig.Main.JoinDialog);
    Assert.True(rig.Main.IsBackgroundEnabled);
  }

  [Fact]
  public void ClosingWhileNothingIsHostedIsAllowedWithoutAsking()
  {
    var rig = new Rig();

    Assert.True(rig.Main.RequestClose());
    Assert.False(rig.Main.HasPrompt);
  }

  [Fact]
  public void ClosingWhileHostingAsksAndDoesNotClose()
  {
    var rig = new Rig().ListenAfterFirstWait();
    rig.Host(Alpha);

    Assert.False(rig.Main.RequestClose());

    Assert.True(rig.Main.HasPrompt);
    Assert.Equal("Stop server and quit", rig.Main.Prompt!.ConfirmLabel);
    Assert.Equal("Keep hosting", rig.Main.Prompt.CancelLabel);
    Assert.False(rig.Runner.Processes[0].Killed);
  }

  [Fact]
  public void ClosingWhileTheServerIsStartingAsks()
  {
    var rig = new Rig();
    bool? allowed = null;
    rig.OnDelay = () =>
    {
      allowed ??= rig.Main.RequestClose();
      rig.Probe.Accepts = true;
    };

    rig.Host(Alpha);

    Assert.False(allowed);
    Assert.True(rig.Main.HasPrompt);
  }

  [Fact]
  public void StopServerAndQuitStopsTheGameAndClosesTheWindow()
  {
    var rig = new Rig().ListenAfterFirstWait();
    var closed = 0;
    rig.Main.CloseWindow = () => closed++;
    rig.Host(Alpha);
    rig.Main.RequestClose();

    rig.Main.Prompt!.ConfirmCommand.Execute(null);

    Assert.True(rig.Runner.Processes[0].Killed);
    Assert.False(rig.Hosting.IsHosting);
    Assert.Equal(1, closed);
    Assert.False(rig.Main.HasPrompt);
    Assert.True(rig.Main.RequestClose());
  }

  [Fact]
  public void KeepHostingMinimizesAndLeavesTheGameRunning()
  {
    var rig = new Rig().ListenAfterFirstWait();
    var minimized = 0;
    var closed = 0;
    rig.Main.MinimizeWindow = () => minimized++;
    rig.Main.CloseWindow = () => closed++;
    rig.Host(Alpha);
    rig.Main.RequestClose();

    rig.Main.Prompt!.CancelCommand.Execute(null);

    Assert.Equal(1, minimized);
    Assert.Equal(0, closed);
    Assert.False(rig.Runner.Processes[0].Killed);
    Assert.True(rig.Hosting.IsHosting);
    Assert.False(rig.Main.RequestClose());
  }

  [Fact]
  public void EscapeOnTheQuitPromptKeepsTheWindowAsItIsWithoutMinimizing()
  {
    var rig = new Rig().ListenAfterFirstWait();
    var minimized = 0;
    rig.Main.MinimizeWindow = () => minimized++;
    rig.Host(Alpha);
    rig.Main.RequestClose();

    rig.Main.CloseOverlayCommand.Execute(null);

    Assert.False(rig.Main.HasPrompt);
    Assert.Equal(0, minimized);
    Assert.False(rig.Runner.Processes[0].Killed);
    Assert.True(rig.Hosting.IsHosting);
  }

  [Fact]
  public void AskingToCloseAgainWhileTheQuitPromptIsOpenKeepsTheSameQuestion()
  {
    var rig = new Rig().ListenAfterFirstWait();
    rig.Host(Alpha);
    rig.Main.RequestClose();
    var prompt = rig.Main.Prompt;

    Assert.False(rig.Main.RequestClose());

    Assert.Same(prompt, rig.Main.Prompt);
  }

  [Fact]
  public void StopServerAndQuitStillClosesWhenTheServerAlreadyExited()
  {
    var rig = new Rig().ListenAfterFirstWait();
    var closed = 0;
    rig.Main.CloseWindow = () => closed++;
    rig.Host(Alpha);
    rig.Main.RequestClose();
    var prompt = rig.Main.Prompt!;
    rig.Runner.Processes[0].Exit(1);

    prompt.ConfirmCommand.Execute(null);

    Assert.Equal(1, closed);
    Assert.True(rig.Main.RequestClose());
  }

  [Fact]
  public void ClosingAfterTheServerEndedOnItsOwnNeedsNoQuestion()
  {
    var rig = new Rig().ListenAfterFirstWait();
    rig.Host(Alpha);
    rig.Runner.Processes[0].Exit(1);

    Assert.True(rig.Main.RequestClose());
    Assert.False(rig.Main.HasPrompt);
  }
}
