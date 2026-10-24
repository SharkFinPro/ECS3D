using System;
using System.IO;
using System.Threading;
using ECS3DLauncher.Services;
using ECS3DLauncher.Shell;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherJoinDialogTests
{
  private readonly LauncherRig _rig = new();
  private readonly FakeProcessRunner _runner = new();

  private MainWindowViewModel NewMain(Action<Action>? post = null) =>
    new(
      _rig.NewEditor(),
      _rig.NewRecents(),
      _runner,
      new EarlyExitWatcher(ShellFixtures.UserData, TimeSpan.FromHours(1)),
      _rig.NewCatalog(),
      _ => null,
      "1.2.3",
      post ?? (action => action()),
      () => _rig.Time.Now);

  private static JoinDialogViewModel NewDialog(LastJoin? last, Action<JoinTarget>? join = null) =>
    new(last, 3000, join ?? (_ => { }), () => { });

  [Fact]
  public void OpensOnTheLastJoinOrTheDefaultPort()
  {
    var fresh = NewDialog(null);
    Assert.Equal("", fresh.Address);
    Assert.Equal("3000", fresh.PortText);

    var again = NewDialog(new LastJoin("192.0.2.24", 3101));
    Assert.Equal("192.0.2.24", again.Address);
    Assert.Equal("3101", again.PortText);
  }

  [Fact]
  public void PastingAddressAndPortFillsBothFields()
  {
    var dialog = NewDialog(null);

    dialog.Address = "192.0.2.24:3101";

    Assert.Equal("192.0.2.24", dialog.Address);
    Assert.Equal("3101", dialog.PortText);
  }

  [Theory]
  [InlineData("", "3000", JoinTarget.AddressError)]
  [InlineData("host", "0", JoinTarget.PortError)]
  [InlineData("host", "65536", JoinTarget.PortError)]
  [InlineData("host", "abc", JoinTarget.PortError)]
  public void InvalidInputShowsTheErrorAndDisablesJoin(string address, string port, string error)
  {
    var dialog = NewDialog(null);
    Assert.Null(dialog.Error);

    dialog.PortText = port;
    dialog.Address = address;

    Assert.Equal(error, dialog.Error);
    Assert.True(dialog.HasError);
    Assert.False(dialog.JoinCommand.CanExecute(null));
  }

  [Fact]
  public void ValidInputEnablesJoinAndPassesTheTarget()
  {
    JoinTarget? joined = null;
    var dialog = NewDialog(null, target => joined = target);

    dialog.Address = " host.example ";
    dialog.PortText = "3001";
    Assert.True(dialog.JoinCommand.CanExecute(null));
    dialog.JoinCommand.Execute(null);

    Assert.Equal(new JoinTarget("host.example", 3001), joined);
  }

  [Fact]
  public void JoinLaunchesTheClientSavesTheJoinAndClosesTheDialog()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    var model = NewMain();
    model.OpenJoinCommand.Execute(null);
    Assert.NotNull(model.JoinDialog);

    model.JoinDialog!.Address = "192.0.2.24:3101";
    model.JoinDialog.JoinCommand.Execute(null);

    var launch = Assert.Single(_runner.Launches);
    Assert.Equal(Path.Combine(LauncherRig.LauncherDir, "ECS3DClient"), launch.FileName);
    Assert.Equal(new[] { "--host", "192.0.2.24", "--port", "3101" }, launch.Arguments);
    Assert.Null(model.JoinDialog);
    var saved = _rig.Saved()!["lastJoin"]!;
    Assert.Equal("192.0.2.24", (string)saved["address"]!);
    Assert.Equal(3101, (int)saved["port"]!);
    Assert.Equal("192.0.2.24:3101", model.Settings.LastJoinText);
  }

  [Fact]
  public void ReopeningStartsFromTheSavedJoin()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    var model = NewMain();
    model.OpenJoinCommand.Execute(null);
    model.JoinDialog!.Address = "host.example:3005";
    model.JoinDialog.JoinCommand.Execute(null);

    model.OpenJoinCommand.Execute(null);

    Assert.Equal("host.example", model.JoinDialog!.Address);
    Assert.Equal("3005", model.JoinDialog.PortText);
  }

  [Fact]
  public void CancelAndTheCloseCommandDismissWithoutLaunching()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    var model = NewMain();

    model.OpenJoinCommand.Execute(null);
    model.JoinDialog!.CancelCommand.Execute(null);
    Assert.Null(model.JoinDialog);

    model.OpenJoinCommand.Execute(null);
    model.CloseJoinCommand.Execute(null);
    Assert.Null(model.JoinDialog);
    Assert.Empty(_runner.Launches);
    Assert.Null(_rig.Saved());
  }

  [Fact]
  public void JoinIsDisabledWithATooltipWhileTheClientIsMissing()
  {
    _rig.AddEngine(LauncherRig.LauncherDir, EngineExecutable.Client);
    var model = NewMain();

    Assert.False(model.OpenJoinCommand.CanExecute(null));
    Assert.Contains("ECS3DClient", model.JoinTooltip);
  }

  [Fact]
  public void AClientThatExitsEarlyRaisesTheToastWithItsLog()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    var model = new MainWindowViewModel(
      _rig.NewEditor(),
      _rig.NewRecents(),
      _runner,
      new EarlyExitWatcher(ShellFixtures.UserData, TimeSpan.FromMinutes(1)),
      _rig.NewCatalog(),
      _ => null,
      "1.2.3",
      action => action(),
      () => _rig.Time.Now);
    model.OpenJoinCommand.Execute(null);
    model.JoinDialog!.Address = "host.example";

    model.JoinDialog.JoinCommand.Execute(null);
    _runner.Last!.Exit(3);

    SpinWait.SpinUntil(() => model.HasToast, TimeSpan.FromSeconds(5));
    Assert.True(model.HasToast);
    Assert.Equal(ShellFixtures.ExpectedLog("client"), model.ToastLogPath);
  }
}
