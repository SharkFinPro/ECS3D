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

  // Cancels as soon as a file with this name has been copied, whatever order the files are copied in.
  private CancellationTokenSource CancelAfterCopyOf(string fileName)
  {
    var source = new CancellationTokenSource();
    _files.AfterCopy = destination =>
    {
      if (destination.EndsWith("/" + fileName, StringComparison.Ordinal))
      {
        Assert.True(_files.DirectoryExists(Hidden("Game")));
        source.Cancel();
      }
    };
    return source;
  }

  [Theory]
  [InlineData("project.ecs3d")]
  [InlineData("A.cs")]
  public void CancellingMidCopyRemovesTheHiddenFolderAndCreatesNothing(string trigger)
  {
    var template = AddTemplate("first-person", "First-Person");
    using var source = CancelAfterCopyOf(trigger);

    var result = NewCreator().Create(template, Projects, "Game", source.Token);

    Assert.True(source.IsCancellationRequested);
    Assert.False(result.Succeeded);
    Assert.Equal(ProjectCreator.CanceledMessage, result.Error);
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
  public void ClosingMidCopyWaitsForTheCleanupThenCloses()
  {
    var (main, work) = OpenDialogWithDeferredWork();
    var closed = 0;
    bool? allowed = null;
    var closedAtRequest = -1;
    main.CloseWindow = () => closed++;
    _files.AfterCopy = destination =>
    {
      if (destination.EndsWith("/A.cs", StringComparison.Ordinal) && allowed is null)
      {
        Assert.True(_files.DirectoryExists(Hidden("Empty 3D")));
        allowed = main.RequestClose();
        closedAtRequest = closed;
      }
    };
    var dialog = main.NewProject!;
    dialog.CreateCommand.Execute(null);
    Assert.True(dialog.IsBusy);

    Drain(work);

    Assert.False(allowed);
    Assert.Equal(0, closedAtRequest);
    Assert.Equal(1, closed);
    Assert.Null(main.NewProject);
    Assert.False(_files.DirectoryExists(Hidden("Empty 3D")));
    Assert.False(_files.DirectoryExists(Folder("Empty 3D")));
    Assert.True(main.RequestClose());
  }

  [Fact]
  public void ASecondCloseRequestWhileCancellingClosesAnyway()
  {
    var (main, work) = OpenDialogWithDeferredWork();
    var closed = 0;
    main.CloseWindow = () => closed++;
    main.NewProject!.CreateCommand.Execute(null);

    Assert.False(main.RequestClose());
    Assert.True(main.RequestClose());
    Drain(work);

    Assert.Equal(1, closed);
  }

  [Fact]
  public void ASystemShutdownCancelsTheCopyButDoesNotRegisterAClose()
  {
    var (main, work) = OpenDialogWithDeferredWork();
    var closed = 0;
    main.CloseWindow = () => closed++;
    main.NewProject!.CreateCommand.Execute(null);

    Assert.True(main.RequestClose(true));
    Drain(work);

    Assert.Equal(0, closed);
    Assert.False(_files.DirectoryExists(Folder("Empty 3D")));
    Assert.False(_files.DirectoryExists(Hidden("Empty 3D")));
  }

  [Fact]
  public void ACancelThatLosesTheRaceToTheCommitStillRecordsTheProject()
  {
    var created = new List<(string Folder, bool Open)>();
    var finished = 0;
    var posted = new List<Action>();
    var dialog = NewDialog(created: (folder, open) => created.Add((folder, open)), post: posted.Add);
    dialog.ProjectName = "Game";
    dialog.CreateCommand.Execute(null);
    Assert.True(_files.FileExists(Path.Combine(Folder("Game"), "project.ecs3d")));

    dialog.CancelCreate(() => finished++);
    posted.ForEach(action => action());

    Assert.Equal(new[] { (Folder("Game"), false) }, created.ToArray());
    Assert.Equal(1, finished);
    Assert.False(dialog.IsBusy);
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
