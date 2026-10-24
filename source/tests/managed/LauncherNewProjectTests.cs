using System;
using System.IO;
using System.Linq;
using ECS3DLauncher.Services;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherNewProjectTests : NewProjectFixture
{
  [Fact]
  public void ACardClickOpensTheDialogForThatTemplate()
  {
    AddTemplate("empty-3d", "Empty 3D");
    AddTemplate("first-person", "First-Person");
    var model = NewMain();
    Assert.Null(model.NewProject);

    model.Templates.OpenCommand.Execute(model.Templates.Templates.Single(card => card.Id == "first-person"));

    Assert.NotNull(model.NewProject);
    Assert.Equal("First-Person", model.NewProject.TemplateName);
    Assert.Equal(LauncherRig.DefaultProjects, model.NewProject.Location);
    Assert.Equal("projects", model.ActiveNav);
  }

  [Fact]
  public void ACardNeedingANewerEngineDoesNotOpenTheDialog()
  {
    AddTemplate("first-person", "First-Person");
    AddTemplate("future", "Future", minEngine: "9.0");
    var model = NewMain();
    var future = model.Templates.Templates.Single(card => card.Id == "future");
    var usable = model.Templates.Templates.Single(card => card.Id == "first-person");

    Assert.False(model.Templates.OpenCommand.CanExecute(future));
    model.Templates.OpenCommand.Execute(future);
    Assert.Null(model.NewProject);

    Assert.True(model.Templates.OpenCommand.CanExecute(usable));
    model.Templates.OpenCommand.Execute(usable);
    Assert.NotNull(model.NewProject);
  }

  [Fact]
  public void TheDialogStartsInTheProjectsFolderFromTheSettings()
  {
    AddTemplate("empty-3d", "Empty 3D");
    var editor = _rig.NewEditor();
    editor.SetProjectsDirectory("/custom/projects");
    var model = NewMain(editor);

    model.NewProjectCommand.Execute(null);

    Assert.Equal("/custom/projects", model.NewProject!.Location);
  }

  [Fact]
  public void TheHeaderButtonPreselectsEmpty3DOnAnyTab()
  {
    AddTemplate("first-person", "First-Person");
    AddTemplate("empty-3d", "Empty 3D");
    var model = NewMain();
    model.ActiveNav = "samples";

    model.NewProjectCommand.Execute(null);

    Assert.Equal("Empty 3D", model.NewProject!.TemplateName);
    Assert.Equal("samples", model.ActiveNav);
    Assert.Null(model.ToastMessage);
  }

  [Fact]
  public void WithoutEmpty3DTheFirstUsableTemplateIsUsed()
  {
    AddTemplate("empty-3d", "Empty 3D", minEngine: "9.0");
    AddTemplate("first-person", "First-Person");

    var model = NewMain();
    model.NewProjectCommand.Execute(null);

    Assert.Equal("First-Person", model.NewProject!.TemplateName);
  }

  [Fact]
  public void TheFirstRunCardOpensTheDialogToo()
  {
    AddTemplate("empty-3d", "Empty 3D");
    var model = NewMain();

    model.Projects.NewProjectCommand.Execute(null);

    Assert.Equal("Empty 3D", model.NewProject!.TemplateName);
    Assert.Same(model.Projects, model.CurrentTab);
  }

  [Fact]
  public void NoTemplatesMeansNoDialogAndAToast()
  {
    var model = NewMain();

    model.NewProjectCommand.Execute(null);

    Assert.Null(model.NewProject);
    Assert.Contains(Templates, model.ToastMessage);
  }

  [Fact]
  public void OnlyGatedTemplatesMeansNoDialogAndAToast()
  {
    AddTemplate("future", "Future", minEngine: "9.0");
    var model = NewMain();

    model.NewProjectCommand.Execute(null);

    Assert.Null(model.NewProject);
    Assert.Contains("newer engine", model.ToastMessage);
  }

  [Fact]
  public void CancelClosesTheDialog()
  {
    AddTemplate("empty-3d", "Empty 3D");
    var model = NewMain();
    model.NewProjectCommand.Execute(null);

    model.NewProject!.CancelCommand.Execute(null);

    Assert.Null(model.NewProject);
    Assert.Empty(_files.EnumerateDirectories(Projects));
  }

  [Fact]
  public void CreatingRecordsTheTemplateAtTheTopAndOpensTheEditorOnce()
  {
    AddTemplate("empty-3d", "Empty 3D");
    _rig.Files.Seed(_rig.RecentsPath, "{\"version\":1,\"projects\":[{\"path\":\"/work/alpha\",\"name\":\"alpha\","
      + "\"lastOpenedUtc\":\"2026-10-18T10:00:00Z\"}]}");
    var model = NewMain();
    model.NewProjectCommand.Execute(null);
    model.NewProject!.ProjectName = "Game";

    model.NewProject.CreateCommand.Execute(null);

    Assert.Null(model.NewProject);
    Assert.True(_files.FileExists(Path.Combine(Folder("Game"), "project.ecs3d")));
    var stored = StoredPaths();
    Assert.Equal(2, stored.Length);
    Assert.EndsWith("Game", stored[0]);
    Assert.Equal("empty-3d", (string)StoredEntry(stored[0])["template"]!);
    Assert.Equal(stored[0], model.Projects.Projects[0].Path);
    var launch = Assert.Single(_runner.Launches);
    Assert.EndsWith("ECS3DEditor", launch.FileName);
    Assert.Equal(new[] { "--project", Folder("Game") }, launch.Arguments.ToArray());
  }

  [Fact]
  public void AnUncheckedBoxRecordsTheProjectWithoutOpeningIt()
  {
    AddTemplate("empty-3d", "Empty 3D");
    var model = NewMain();
    model.NewProjectCommand.Execute(null);
    model.NewProject!.OpenInEditor = false;

    model.NewProject.CreateCommand.Execute(null);

    Assert.Null(model.NewProject);
    Assert.Empty(_runner.Launches);
    Assert.Null(model.ToastMessage);
    Assert.Equal("empty-3d", (string)StoredEntry(Assert.Single(StoredPaths()))["template"]!);
  }

  [Fact]
  public void AMissingEditorStillCreatesAndRecordsTheProjectAndSaysSo()
  {
    AddTemplate("empty-3d", "Empty 3D");
    _rig.AddEngine(LauncherRig.LauncherDir, EngineExecutable.Editor);
    var model = NewMainWithCurrentEngine();
    model.NewProjectCommand.Execute(null);
    var folder = Path.Combine(Projects, model.NewProject!.ProjectName);

    model.NewProject.CreateCommand.Execute(null);

    Assert.Null(model.NewProject);
    Assert.Empty(_runner.Launches);
    Assert.True(_files.FileExists(Path.Combine(folder, "project.ecs3d")));
    Assert.Equal("empty-3d", (string)StoredEntry(Assert.Single(StoredPaths()))["template"]!);
    Assert.Contains(folder, model.ToastMessage);
    Assert.Contains("ECS3DEditor", model.ToastMessage);
  }

  [Fact]
  public void AFailedCreateKeepsTheDialogAndRecordsNothing()
  {
    AddTemplate("empty-3d", "Empty 3D");
    _files.CopyFailure = new UnauthorizedAccessException("access denied");
    var model = NewMain();
    model.NewProjectCommand.Execute(null);
    var dialog = model.NewProject!;

    dialog.CreateCommand.Execute(null);

    Assert.Same(dialog, model.NewProject);
    Assert.Contains("access denied", dialog.CreateError);
    Assert.False(dialog.IsBusy);
    Assert.Empty(_runner.Launches);
    Assert.Null(_rig.Files.TextOf(_rig.RecentsPath));
    Assert.Empty(model.Projects.Projects);
  }

  [Fact]
  public void AnOpenDialogBlocksTheCommandsBehindIt()
  {
    AddTemplate("empty-3d", "Empty 3D");
    AddTemplate("first-person", "First-Person");
    _rig.Files.Seed(_rig.RecentsPath, "{\"version\":1,\"projects\":[{\"path\":\"/work/alpha\",\"name\":\"alpha\","
      + "\"lastOpenedUtc\":\"2026-10-18T10:00:00Z\"}]}");
    _files.Seed("/work/alpha/project.ecs3d", "{}");
    var model = NewMain();
    var card = model.Projects.Projects.Single();
    model.NewProjectCommand.Execute(null);
    var dialog = model.NewProject!;
    Assert.False(model.IsInteractive);

    model.OpenProjectCommand.Execute(card);
    model.PlayProjectCommand.Execute(card);
    model.RemoveProjectCommand.Execute(card);
    model.TogglePinCommand.Execute(card);
    model.ExploreCommand.Execute(null);
    model.SelectNavCommand.Execute("samples");
    model.Templates.OpenCommand.Execute(model.Templates.Templates.Single(item => item.Id == "first-person"));

    Assert.Empty(_runner.Launches);
    Assert.Equal(new[] { "/work/alpha" }, StoredPaths());
    Assert.False((bool?)StoredEntry("/work/alpha")["pinned"] ?? false);
    Assert.Equal("projects", model.ActiveNav);
    Assert.Same(dialog, model.NewProject);

    dialog.CancelCommand.Execute(null);

    Assert.True(model.IsInteractive);
    model.OpenProjectCommand.Execute(card);
    Assert.Single(_runner.Launches);
  }
}
