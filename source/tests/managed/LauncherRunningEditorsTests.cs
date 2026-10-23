using System;
using System.Threading;
using System.Threading.Tasks;
using ECS3DLauncher.Services;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherRunningEditorsTests
{
  private const string Alpha = "/work/alpha";
  private const string Beta = "/work/beta";

  private static readonly TimeSpan LongWindow = TimeSpan.FromHours(1);
  private static readonly TimeSpan ShortWindow = TimeSpan.FromMilliseconds(50);
  private static readonly TimeSpan Patience = TimeSpan.FromSeconds(5);

  private readonly LauncherRig _rig = new();
  private readonly RecordingProcessRunner _runner = new();
  private readonly NoticeRecorder _notices = new();
  private int _changes;

  private RunningEditors NewEditors() => new(_rig.NewRecents().ProjectKey, () => Interlocked.Increment(ref _changes));

  private LaunchCoordinator NewCoordinator(RunningEditors editors, TimeSpan window, params EngineExecutable[] missing) =>
    new(
      new EngineLauncher(_runner, ShellFixtures.EngineWithout(missing)),
      new EarlyExitWatcher(ShellFixtures.UserData, window),
      _notices.Record,
      editors);

  [Fact]
  public void TwoEditorsOfOneProjectCountSeparatelyAndProjectsAreIndependent()
  {
    var editors = NewEditors();

    editors.Started(Alpha);
    editors.Started(Alpha);
    editors.Started(Beta);
    editors.Stopped(Alpha);

    Assert.True(editors.IsRunning(Alpha));
    Assert.True(editors.IsRunning(Beta));

    editors.Stopped(Alpha);
    Assert.False(editors.IsRunning(Alpha));
    Assert.True(editors.IsRunning(Beta));
    Assert.Equal(5, _changes);
  }

  [Fact]
  public void AnUnbalancedStopDoesNotCarryOverToTheNextEditor()
  {
    var editors = NewEditors();

    editors.Stopped(Alpha);
    Assert.False(editors.IsRunning(Alpha));

    editors.Started(Alpha);
    Assert.True(editors.IsRunning(Alpha));

    editors.Stopped(Alpha);
    Assert.False(editors.IsRunning(Alpha));
  }

  [Fact]
  public void SpellingsOfOnePathShareACount()
  {
    var editors = NewEditors();

    editors.Started("/work/alpha/");
    Assert.True(editors.IsRunning(Alpha));
    Assert.True(editors.IsRunning("/work/alpha/project.ecs3d"));
    Assert.False(editors.IsRunning(Beta));

    editors.Stopped("/work/alpha/project.ecs3d");
    Assert.False(editors.IsRunning(Alpha));
  }

  [Fact]
  public async Task AFailedStartOrAMissingEditorIsNotCounted()
  {
    var editors = NewEditors();
    _runner.FailFor = _ => "could not start";
    await NewCoordinator(editors, LongWindow).OpenProjectAsync(Alpha, ShellFixtures.NoOptions);

    await NewCoordinator(editors, LongWindow, EngineExecutable.Editor).OpenProjectAsync(Alpha, ShellFixtures.NoOptions);

    Assert.False(editors.IsRunning(Alpha));
    Assert.Equal(0, _changes);
    Assert.Equal(2, _notices.Notices.Count);

    _runner.FailFor = null;
    _ = NewCoordinator(editors, LongWindow).OpenProjectAsync(Alpha, ShellFixtures.NoOptions);
    Assert.True(editors.IsRunning(Alpha));
  }

  [Fact]
  public async Task AQuickNonzeroExitIsReportedAndStillStopsTheCount()
  {
    var editors = NewEditors();
    var task = NewCoordinator(editors, LongWindow).OpenProjectAsync(Alpha, ShellFixtures.NoOptions);
    Assert.True(editors.IsRunning(Alpha));

    _runner.Processes[0].Exit(3);
    await task;

    Assert.True(SpinWait.SpinUntil(() => _runner.Processes[0].DisposeCount == 1 && !editors.IsRunning(Alpha), Patience));
    Assert.Contains("exited with code 3", Assert.Single(_notices.Notices).Message);
  }

  [Fact]
  public async Task PlayAndExploreAreNotTrackedAndTheirHandlesAreReleased()
  {
    var editors = NewEditors();
    var coordinator = NewCoordinator(editors, ShortWindow);

    await coordinator.PlayProjectAsync(Alpha);
    await coordinator.ExploreEngineAsync(ShellFixtures.NoOptions);

    Assert.Equal(2, _runner.Processes.Count);
    Assert.False(editors.IsRunning(Alpha));
    Assert.Equal(0, _changes);
    Assert.All(_runner.Processes, process => Assert.Equal(1, process.DisposeCount));
  }

  [Fact]
  public async Task ARunningEditorsHandleIsReleasedOnceAndOnlyAfterItExits()
  {
    var editors = NewEditors();
    await NewCoordinator(editors, ShortWindow).OpenProjectAsync(Alpha, ShellFixtures.NoOptions);

    Assert.True(editors.IsRunning(Alpha));
    Assert.Equal(0, _runner.Processes[0].DisposeCount);

    _runner.Processes[0].Exit(0);

    Assert.True(SpinWait.SpinUntil(() => !editors.IsRunning(Alpha), Patience));
    Assert.Equal(1, _runner.Processes[0].DisposeCount);
    Assert.False(_runner.Processes[0].Killed);
  }
}
