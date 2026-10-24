using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using ECS3DLauncher.Services;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherNewProjectDialogTests : NewProjectFixture
{

  [Fact]
  public void TheNameDefaultsToTheTemplateNameWhileItIsFree()
  {
    var dialog = NewDialog();

    Assert.Equal("First-Person", dialog.ProjectName);
    Assert.Equal("First-Person", dialog.TemplateName);
    Assert.Equal("About First-Person", dialog.TemplateDescription);
    Assert.Equal(Projects, dialog.Location);
    Assert.True(dialog.OpenInEditor);
    Assert.True(dialog.CanCreate);
  }

  [Fact]
  public void ATakenDefaultNameGetsTheNextFreeNumber()
  {
    _files.Seed(Path.Combine(Folder("First-Person"), "project.ecs3d"), "{}");
    Assert.Equal("First-Person 2", NewDialog().ProjectName);

    _files.Seed(Path.Combine(Folder("First-Person 2"), "project.ecs3d"), "{}");
    Assert.Equal("First-Person 3", NewDialog().ProjectName);
  }

  [Fact]
  public void AnEmptyFolderDoesNotTakeTheDefaultName()
  {
    _files.AddDirectory(Folder("First-Person"));

    Assert.Equal("First-Person", NewDialog().ProjectName);
  }

  [Theory]
  [InlineData("", "Enter a project name.")]
  [InlineData("LONG", "Use at most 64 characters.")]
  [InlineData("a/b", "The name cannot contain")]
  [InlineData("name.", "start or end with a space or a dot")]
  [InlineData("CON", "reserved name")]
  public void AnInvalidNameShowsItsMessageAndBlocksCreate(string name, string message)
  {
    var dialog = NewDialog();
    Assert.True(dialog.CanCreate);

    dialog.ProjectName = name == "LONG" ? new string('a', 65) : name;

    Assert.Contains(message, dialog.NameError);
    Assert.True(dialog.HasNameError);
    Assert.False(dialog.CanCreate);
    Assert.False(dialog.CreateCommand.CanExecute(null));
    Assert.Equal("", dialog.TargetPreview);
    Assert.Null(dialog.TargetState);

    dialog.ProjectName = "Fine";

    Assert.Null(dialog.NameError);
    Assert.False(dialog.HasNameError);
    Assert.True(dialog.CanCreate);
    Assert.True(dialog.CreateCommand.CanExecute(null));
  }

  [Fact]
  public void ThePreviewShowsTheProjectFileInTheNewFolder()
  {
    var dialog = NewDialog();
    dialog.ProjectName = "Game";

    Assert.Equal(Path.Combine(Projects, "Game", "project.ecs3d"), dialog.TargetPreview);
    Assert.Equal("Folder name is free", dialog.TargetMessage);
    Assert.True(dialog.IsTargetFree);
    Assert.False(dialog.HasTargetProblem);
  }

  [Fact]
  public void OnlyAFreeTargetEnablesCreate()
  {
    _files.Seed(Path.Combine(Folder("Used"), "x.txt"), "x");
    _files.Seed(Folder("Taken"), "a file");
    var dialog = NewDialog();

    dialog.ProjectName = "Used";
    Assert.Equal(ProjectTargetState.NotEmpty, dialog.TargetState);
    Assert.Equal("A folder with this name already exists and is not empty.", dialog.TargetMessage);
    Assert.True(dialog.HasTargetProblem);
    Assert.False(dialog.IsTargetFree);
    Assert.False(dialog.CanCreate);

    dialog.ProjectName = "Taken";
    Assert.Equal(ProjectTargetState.NotAFolder, dialog.TargetState);
    Assert.Equal("A file with this name already exists.", dialog.TargetMessage);
    Assert.False(dialog.CanCreate);

    _files.UnreadableUnder = Folder("Locked");
    dialog.ProjectName = "Locked";
    Assert.Equal(ProjectTargetState.Unavailable, dialog.TargetState);
    Assert.Equal("This location cannot be used.", dialog.TargetMessage);
    Assert.False(dialog.CanCreate);

    dialog.ProjectName = "Fresh";
    Assert.Equal(ProjectTargetState.Free, dialog.TargetState);
    Assert.True(dialog.CanCreate);
  }

  [Fact]
  public void ALocationThatIsAFileOrBlankOrMalformedBlocksCreate()
  {
    _files.Seed("/docs/afile", "x");
    var dialog = NewDialog();
    Assert.True(dialog.CanCreate);

    dialog.Location = "/docs/afile";
    Assert.Equal("That location is a file, not a folder.", dialog.LocationError);
    Assert.False(dialog.CanCreate);
    Assert.Null(dialog.TargetState);

    dialog.Location = "  ";
    Assert.Equal("Enter a location.", dialog.LocationError);
    Assert.False(dialog.CanCreate);

    dialog.Location = "bad\0path";
    Assert.Equal("This location cannot be used.", dialog.LocationError);
    Assert.False(dialog.CanCreate);

    dialog.Location = "/docs/elsewhere";
    Assert.Null(dialog.LocationError);
    Assert.False(dialog.HasLocationError);
    Assert.True(dialog.CanCreate);
  }

  [Fact]
  public void ChangingTheLocationChecksTheNameThereAgain()
  {
    _files.Seed(Path.Combine("/docs/other", "Game", "x.txt"), "x");
    var dialog = NewDialog();
    dialog.ProjectName = "Game";
    Assert.True(dialog.CanCreate);

    dialog.Location = "/docs/other";

    Assert.Equal(ProjectTargetState.NotEmpty, dialog.TargetState);
    Assert.Equal(Path.Combine("/docs/other", "Game", "project.ecs3d"), dialog.TargetPreview);
    Assert.False(dialog.CanCreate);
  }

  [Fact]
  public void BusyDisablesCreateAndCancelUntilTheCopyFinishes()
  {
    var work = new List<Action>();
    var created = new List<(string Folder, bool Open)>();
    var closed = 0;
    var dialog = NewDialog(background: work.Add, created: (folder, open) => created.Add((folder, open)), close: () => closed++);
    dialog.ProjectName = "Game";

    dialog.CreateCommand.Execute(null);

    Assert.True(dialog.IsBusy);
    Assert.False(dialog.CanCreate);
    Assert.False(dialog.CreateCommand.CanExecute(null));
    Assert.False(dialog.CancelCommand.CanExecute(null));
    Assert.False(dialog.BrowseCommand.CanExecute(null));
    dialog.CancelCommand.Execute(null);
    dialog.CreateCommand.Execute(null);
    Assert.Equal(0, closed);
    Assert.Single(work);
    Assert.False(_files.DirectoryExists(Folder("Game")));

    work[0]();

    Assert.False(dialog.IsBusy);
    Assert.Equal(new[] { (Folder("Game"), true) }, created.ToArray());
    Assert.True(_files.FileExists(Path.Combine(Folder("Game"), "project.ecs3d")));
    Assert.True(dialog.CancelCommand.CanExecute(null));
    dialog.CancelCommand.Execute(null);
    Assert.Equal(1, closed);
  }

  [Fact]
  public void TheEditorChoiceReachesTheCallback()
  {
    var created = new List<bool>();
    var dialog = NewDialog(created: (_, open) => created.Add(open));
    dialog.ProjectName = "Game";
    dialog.OpenInEditor = false;

    dialog.CreateCommand.Execute(null);

    Assert.Equal(new[] { false }, created.ToArray());
  }

  [Theory]
  [InlineData("denied")]
  [InlineData("unexpected")]
  public void AFailedCopyKeepsTheDialogOpenWithTheReason(string kind)
  {
    _files.CopyFailure = kind == "denied"
      ? new UnauthorizedAccessException("access denied")
      : new InvalidOperationException("boom");
    var created = 0;
    var dialog = NewDialog(created: (_, _) => created++);
    dialog.ProjectName = "Game";

    dialog.CreateCommand.Execute(null);

    Assert.Equal(0, created);
    Assert.False(dialog.IsBusy);
    Assert.True(dialog.HasCreateError);
    Assert.Contains(kind == "denied" ? "access denied" : "boom", dialog.CreateError);
    Assert.True(dialog.CanCreate);
    Assert.True(dialog.CancelCommand.CanExecute(null));
    Assert.False(_files.DirectoryExists(Folder("Game")));

    dialog.ProjectName = "Game2";
    Assert.Null(dialog.CreateError);
    Assert.False(dialog.HasCreateError);
  }

  [Fact]
  public async Task BrowseSetsTheChosenFolderAndIgnoresACancel()
  {
    var answers = new Queue<string?>(["/docs/picked", null, ""]);
    var dialog = NewDialog(pick: () => Task.FromResult(answers.Dequeue()));

    dialog.BrowseCommand.Execute(null);
    await Task.Yield();
    Assert.Equal("/docs/picked", dialog.Location);

    dialog.BrowseCommand.Execute(null);
    await Task.Yield();
    Assert.Equal("/docs/picked", dialog.Location);
    Assert.Null(dialog.CreateError);

    dialog.BrowseCommand.Execute(null);
    await Task.Yield();
    Assert.Equal("/docs/picked", dialog.Location);
    Assert.Equal("Choose a folder on this computer.", dialog.CreateError);
  }

  [Fact]
  public void AnExceptionOfAnyKindClearsBusyAndShowsAnError()
  {
    _files.CopyFailure = new OperationCanceledException("stopped");
    var dialog = NewDialog();

    dialog.CreateCommand.Execute(null);

    Assert.False(dialog.IsBusy);
    Assert.Contains("stopped", dialog.CreateError);
    Assert.True(dialog.CanCreate);
  }

  [Fact]
  public void TheEditorChoiceIsTakenWhenCreateStarts()
  {
    var work = new List<Action>();
    var created = new List<bool>();
    var dialog = NewDialog(background: work.Add, created: (_, open) => created.Add(open));

    dialog.CreateCommand.Execute(null);
    dialog.OpenInEditor = false;
    work[0]();

    Assert.Equal(new[] { true }, created.ToArray());
  }

  [Fact]
  public async Task ABrowseResultAfterTheDialogClosedIsIgnored()
  {
    var picked = new TaskCompletionSource<string?>();
    var dialog = NewDialog(pick: () => picked.Task);
    var before = dialog.Location;

    dialog.BrowseCommand.Execute(null);
    dialog.CancelCommand.Execute(null);
    picked.SetResult("/docs/late");
    await Task.Yield();

    Assert.Equal(before, dialog.Location);
  }
}
