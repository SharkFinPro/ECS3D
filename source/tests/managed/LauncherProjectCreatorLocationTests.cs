using System;
using System.IO;
using ECS3DLauncher.Services;
using Xunit;

namespace ECS3DManagedTests;

// The location checks and the plain failure messages, over the in-memory tree.
public class LauncherProjectCreatorLocationTests : NewProjectFixture
{
  private static readonly string Root = Path.GetPathRoot(Projects)!;

  private void MakeRootUnreachable()
  {
    _files.RemoveRoot(Root);
  }

  [Fact]
  public void ALocationWhoseRootIsMissingIsUnreachable()
  {
    MakeRootUnreachable();

    Assert.Equal(ProjectTargetState.Unreachable, NewCreator().CheckLocation(Projects));
  }

  [Fact]
  public void AMissingFolderOnAnExistingRootIsFree()
  {
    _files.AddDirectory(Root);

    Assert.Equal(ProjectTargetState.Free, NewCreator().CheckLocation(Projects));
  }

  [Fact]
  public void AFileAtTheLocationIsStillNotAFolder()
  {
    _files.Seed(Path.Combine(Projects, "afile"), "x");

    Assert.Equal(ProjectTargetState.NotAFolder, NewCreator().CheckLocation(Path.Combine(Projects, "afile")));
  }

  [Fact]
  public void ARelativeLocationHasNoRootToCheck()
  {
    var creator = NewCreator();
    _files.Seed(Path.Combine("rel", "afile"), "x");

    Assert.Equal(ProjectTargetState.Free, creator.CheckLocation(Path.Combine("rel", "dir")));
    Assert.Equal(ProjectTargetState.NotAFolder, creator.CheckLocation(Path.Combine("rel", "afile")));
  }

  [Fact]
  public void AMissingDriveOrUncShareIsUnreachableAndAPresentOneIsNot()
  {
    if (!OperatingSystem.IsWindows())
    {
      return;
    }

    var creator = NewCreator();
    _files.AddDirectory(@"\\present\share");

    Assert.Equal(ProjectTargetState.Unreachable, creator.CheckLocation(@"Q:\Projects"));
    Assert.Equal(ProjectTargetState.Unreachable, creator.CheckLocation(@"\\nosuchhost\share\x"));
    Assert.Equal(ProjectTargetState.Free, creator.CheckLocation(@"\\present\share\x"));
  }

  [Fact]
  public void CreateRefusesAnUnreachableLocationBeforeTouchingTheDisk()
  {
    var template = AddTemplate("empty-3d", "Empty 3D");
    MakeRootUnreachable();

    var result = NewCreator().Create(template, Projects, "Game");

    Assert.False(result.Succeeded);
    Assert.Equal(ProjectCreator.UnreachableMessage, result.Error);
    Assert.False(_files.DirectoryExists(Projects));
    Assert.Empty(_rig.Log.Warnings);
  }

  [Fact]
  public void APermissionFailureIsPlainAndTheRawTextIsLogged()
  {
    var template = AddTemplate("empty-3d", "Empty 3D");
    _files.CopyFailure = new UnauthorizedAccessException("Access to the path 'x' is denied.");

    var result = NewCreator().Create(template, Projects, "Game");

    Assert.Equal(
      $"Could not create the project in {Projects}. You do not have permission to write there.",
      result.Error);
    Assert.Contains(_rig.Log.Warnings, line => line.Contains("Access to the path 'x' is denied."));
  }

  [Fact]
  public void AnIoFailureKeepsOnlyTheShortReasonAndTheRawTextIsLogged()
  {
    var template = AddTemplate("empty-3d", "Empty 3D");
    _files.CopyFailure = new IOException("The network path was not found. : '\\\\nosuchhost\\share\\x'");

    var result = NewCreator().Create(template, Projects, "Game");

    Assert.Equal($"Could not create the project in {Projects}: The network path was not found.", result.Error);
    Assert.Contains(_rig.Log.Warnings, line => line.Contains("'\\\\nosuchhost\\share\\x'"));
  }

  [Fact]
  public void AnIoFailureOnARootThatVanishedReportsTheLocationUnreachable()
  {
    var template = AddTemplate("empty-3d", "Empty 3D");
    _files.CopyFailure = new IOException("The network path was not found.");
    _files.AfterCopy = _ => _files.RemoveRoot(Root);

    var result = NewCreator().Create(template, Projects, "Game");

    Assert.Equal(ProjectCreator.UnreachableMessage, result.Error);
    Assert.Single(_rig.Log.Warnings);
  }

  [Fact]
  public void ADamagedTemplateStillSaysSo()
  {
    var template = AddTemplate("empty-3d", "Empty 3D");
    _files.Seed(Path.Combine(template.FolderPath, "project.ecs3d"), "{");

    var result = NewCreator().Create(template, Projects, "Game");

    Assert.Equal("Could not create the project: the template's project file is not valid.", result.Error);
  }
}
