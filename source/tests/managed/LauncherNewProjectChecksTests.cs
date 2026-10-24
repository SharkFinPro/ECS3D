using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using ECS3DLauncher.Services;
using ECS3DLauncher.Tabs.Templates;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherNewProjectChecksTests : NewProjectFixture
{
  private readonly List<TaskCompletionSource> _delays = [];
  private readonly List<(Func<TargetCheck> Work, TaskCompletionSource<TargetCheck> Done)> _checks = [];

  private Task WaitedDelay(TimeSpan _)
  {
    var gate = new TaskCompletionSource();
    _delays.Add(gate);
    return gate.Task;
  }

  private Task<TargetCheck> HeldCheck(Func<TargetCheck> work)
  {
    var done = new TaskCompletionSource<TargetCheck>();
    _checks.Add((work, done));
    return done.Task;
  }

  private NewProjectViewModel NewHeldDialog(Action<Action>? background = null)
  {
    var dialog = NewDialog(background: background, delay: WaitedDelay, runCheck: HeldCheck);
    FinishDelays();
    FinishChecks();
    return dialog;
  }

  private void FinishDelays()
  {
    _delays.ForEach(gate => gate.TrySetResult());
  }

  private void FinishChecks()
  {
    foreach (var (work, done) in _checks)
    {
      done.TrySetResult(work());
    }

    _checks.Clear();
  }

  [Fact]
  public void AnEditShowsCheckingAndBlocksCreateUntilTheCheckAnswers()
  {
    var dialog = NewHeldDialog();
    Assert.False(dialog.IsChecking);
    Assert.True(dialog.CanCreate);

    dialog.ProjectName = "Game";

    Assert.True(dialog.IsChecking);
    Assert.Equal("Checking...", dialog.TargetPreview);
    Assert.Equal("Checking...", dialog.TargetMessage);
    Assert.Null(dialog.TargetState);
    Assert.False(dialog.CanCreate);
    Assert.False(dialog.CreateCommand.CanExecute(null));

    FinishDelays();
    FinishChecks();

    Assert.False(dialog.IsChecking);
    Assert.Equal(Path.Combine(Projects, "Game", "project.ecs3d"), dialog.TargetPreview);
    Assert.Equal("Folder name is free", dialog.TargetMessage);
    Assert.True(dialog.CanCreate);
    Assert.True(dialog.CreateCommand.CanExecute(null));
  }

  [Fact]
  public void ANameErrorShowsAtOnceAndAValidNameWaitsForTheDisk()
  {
    var dialog = NewHeldDialog();

    dialog.ProjectName = "a/b";

    Assert.Contains("The name cannot contain", dialog.NameError);
    Assert.False(dialog.CanCreate);
    Assert.True(dialog.IsChecking);
    Assert.Empty(_checks);

    dialog.ProjectName = "Game";

    Assert.Null(dialog.NameError);
    Assert.False(dialog.CanCreate);
    Assert.Empty(_checks);
    FinishDelays();
    Assert.Single(_checks);
    FinishChecks();
    Assert.True(dialog.CanCreate);
  }

  [Fact]
  public void RapidEditsRunOneCheckForTheFinalInput()
  {
    _files.Seed(Path.Combine(Folder("Final"), "x.txt"), "x");
    var dialog = NewHeldDialog();
    _delays.Clear();

    dialog.ProjectName = "F";
    dialog.ProjectName = "Fi";
    dialog.ProjectName = "Final";
    Assert.Equal(3, _delays.Count);
    FinishDelays();

    Assert.Single(_checks);
    FinishChecks();
    Assert.Equal(ProjectTargetState.NotEmpty, dialog.TargetState);
    Assert.False(dialog.CanCreate);
  }

  [Fact]
  public void AStaleResultIsDiscardedAndTheFreshOneApplied()
  {
    _files.Seed(Path.Combine(Folder("Old"), "x.txt"), "x");
    var dialog = NewHeldDialog();
    _delays.Clear();

    dialog.ProjectName = "Old";
    FinishDelays();
    var stale = _checks[0];
    dialog.ProjectName = "New";
    FinishDelays();
    Assert.Equal(2, _checks.Count);

    stale.Done.SetResult(stale.Work());

    Assert.True(dialog.IsChecking);
    Assert.Null(dialog.TargetState);
    Assert.False(dialog.CanCreate);

    var fresh = _checks[1];
    fresh.Done.SetResult(fresh.Work());

    Assert.False(dialog.IsChecking);
    Assert.Equal(ProjectTargetState.Free, dialog.TargetState);
    Assert.Equal(Path.Combine(Projects, "New", "project.ecs3d"), dialog.TargetPreview);
    Assert.True(dialog.CanCreate);
  }

  [Fact]
  public void ALocationEditIsCheckedOffTheUiThreadToo()
  {
    _files.Seed("/docs/afile", "x");
    var dialog = NewHeldDialog();

    dialog.Location = "/docs/afile";
    Assert.Null(dialog.LocationError);
    Assert.True(dialog.IsChecking);
    FinishDelays();
    FinishChecks();

    Assert.Equal("That location is a file, not a folder.", dialog.LocationError);
    Assert.False(dialog.CanCreate);
  }

  [Fact]
  public void ABlankLocationIsRejectedAtOnce()
  {
    var dialog = NewHeldDialog();

    dialog.Location = "  ";

    Assert.Equal("Enter a location.", dialog.LocationError);
    Assert.False(dialog.IsChecking);
    Assert.False(dialog.CanCreate);
  }

  [Fact]
  public void AThrowingCheckBecomesUnavailableAndClearsChecking()
  {
    var dialog = NewHeldDialog();

    dialog.ProjectName = "Game";
    FinishDelays();
    _checks[0].Done.SetException(new InvalidOperationException("boom"));
    _checks.Clear();

    Assert.False(dialog.IsChecking);
    Assert.Equal(ProjectTargetState.Unavailable, dialog.TargetState);
    Assert.False(dialog.CanCreate);

    dialog.ProjectName = "Game2";
    FinishDelays();
    FinishChecks();
    Assert.Equal(ProjectTargetState.Free, dialog.TargetState);
    Assert.True(dialog.CanCreate);
  }

  [Fact]
  public void AThrowingCheckForSupersededInputsIsIgnored()
  {
    var dialog = NewHeldDialog();

    dialog.ProjectName = "Old";
    FinishDelays();
    var stale = _checks[0];
    dialog.ProjectName = "New";

    stale.Done.SetException(new InvalidOperationException("boom"));

    Assert.True(dialog.IsChecking);
    Assert.Null(dialog.TargetState);
    Assert.False(dialog.CanCreate);
  }
}
