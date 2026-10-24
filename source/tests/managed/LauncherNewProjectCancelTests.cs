using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using ECS3DLauncher.Services;
using ECS3DLauncher.Shell;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherNewProjectCancelTests : NewProjectFixture
{
  private static string Hidden(string name) => Path.Combine(Projects, ProjectCreator.HiddenFolderName(name));

  [Fact]
  public void CancellingMidCopyRemovesTheHiddenFolderAndCreatesNothing()
  {
    var template = AddTemplate("first-person", "First-Person");
    using var source = new CancellationTokenSource();
    var copied = 0;
    _files.AfterCopy = _ =>
    {
      if (++copied == 1)
      {
        Assert.True(_files.DirectoryExists(Hidden("Game")));
        source.Cancel();
      }
    };

    var result = NewCreator().Create(template, Projects, "Game", source.Token);

    Assert.False(result.Succeeded);
    Assert.Equal(ProjectCreator.CanceledMessage, result.Error);
    Assert.Equal(1, copied);
    Assert.False(_files.DirectoryExists(Hidden("Game")));
    Assert.False(_files.DirectoryExists(Folder("Game")));
  }

  [Fact]
  public void ACopyThatIsNotCancelledCommitsTheProject()
  {
    var template = AddTemplate("first-person", "First-Person");
    using var source = new CancellationTokenSource();

    var result = NewCreator().Create(template, Projects, "Game", source.Token);

    Assert.True(result.Succeeded);
    Assert.True(_files.FileExists(Path.Combine(Folder("Game"), "project.ecs3d")));
    Assert.True(_files.FileExists(Path.Combine(Folder("Game"), "scripts", "A.cs")));
    Assert.False(_files.DirectoryExists(Hidden("Game")));
  }

  [Fact]
  public void CancellingBeforeTheCommitStillLeavesNothingBehind()
  {
    var template = AddTemplate("first-person", "First-Person");
    using var source = new CancellationTokenSource();
    _files.AfterCopy = destination =>
    {
      if (destination.EndsWith("A.cs", StringComparison.Ordinal))
      {
        source.Cancel();
      }
    };

    var result = NewCreator().Create(template, Projects, "Game", source.Token);

    Assert.False(result.Succeeded);
    Assert.False(_files.DirectoryExists(Hidden("Game")));
    Assert.False(_files.DirectoryExists(Folder("Game")));
  }

  private (MainWindowViewModel Main, List<Action> Work) OpenDialogWithDeferredWork()
  {
    AddTemplate("empty-3d", "Empty 3D");
    var work = new List<Action>();
    var main = NewMain(background: work.Add);
    main.NewProjectCommand.Execute(null);
    Drain(work);
    return (main, work);
  }

  private static void Drain(List<Action> work)
  {
    foreach (var item in work.ToArray())
    {
      item();
    }

    work.Clear();
  }

  [Fact]
  public void ClosingWhileACopyRunsWaitsForTheCleanupThenCloses()
  {
    var (main, work) = OpenDialogWithDeferredWork();
    var closed = 0;
    main.CloseWindow = () => closed++;
    var dialog = main.NewProject!;
    dialog.CreateCommand.Execute(null);
    Assert.True(dialog.IsBusy);

    Assert.False(main.RequestClose());

    Assert.Equal(0, closed);
    Assert.NotNull(main.NewProject);

    Drain(work);

    Assert.Equal(1, closed);
    Assert.Null(main.NewProject);
    Assert.False(_files.DirectoryExists(Hidden("Empty 3D")));
    Assert.False(_files.DirectoryExists(Folder("Empty 3D")));
    Assert.True(main.RequestClose());
  }

  [Fact]
  public void ClosingWhileACopyRunsTwiceStillClosesOnce()
  {
    var (main, work) = OpenDialogWithDeferredWork();
    var closed = 0;
    main.CloseWindow = () => closed++;
    main.NewProject!.CreateCommand.Execute(null);

    Assert.False(main.RequestClose());
    Assert.False(main.RequestClose());
    Drain(work);

    Assert.Equal(1, closed);
  }

  [Fact]
  public void ClosingWithNoCopyRunningClosesAtOnce()
  {
    var (main, work) = OpenDialogWithDeferredWork();
    var closed = 0;
    main.CloseWindow = () => closed++;
    Assert.NotNull(main.NewProject);

    Assert.True(main.RequestClose());
    Assert.Equal(0, closed);

    main.NewProject!.CreateCommand.Execute(null);
    Drain(work);

    Assert.True(_files.FileExists(Path.Combine(Folder("Empty 3D"), "project.ecs3d")));
    Assert.True(main.RequestClose());
    Assert.Equal(0, closed);
  }

  [Fact]
  public void CancelCreateDoesNothingWhileNoCopyRuns()
  {
    var dialog = NewDialog();
    var finished = 0;

    dialog.CancelCreate(() => finished++);

    Assert.Equal(0, finished);
    Assert.False(dialog.IsCreating);
  }
}
