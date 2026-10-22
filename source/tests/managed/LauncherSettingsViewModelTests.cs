using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using ECS3DLauncher.Services;
using ECS3DLauncher.Tabs.Settings;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherSettingsViewModelTests
{
  private readonly LauncherRig _rig = new();
  private readonly List<SettingsUpdate> _updates = [];
  private readonly List<string> _failures = [];

  private SettingsViewModel NewModel(string? settingsJson = null)
  {
    if (settingsJson is not null)
    {
      _rig.Files.Seed(_rig.SettingsPath, settingsJson);
    }

    return new SettingsViewModel(_rig.NewEditor(), _updates.Add, _failures.Add);
  }

  [Fact]
  public void InitialValuesMirrorTheEditor()
  {
    var model = NewModel("""{ "projectsDirectory": "/work/games", "editorConsole": true, "hostPort": 4100 }""");

    Assert.Equal("", model.EngineFolder);
    Assert.Equal("/work/games", model.ProjectsFolder);
    Assert.Equal("4100", model.PortText);
    Assert.True(model.EditorConsole);
    Assert.False(model.ServerConsole);
    Assert.False(model.CloseAfterOpen);
    Assert.Equal("-", model.LastJoinText);
    Assert.False(model.HasPortError);
    Assert.False(model.HasEngineError);
  }

  [Fact]
  public void TogglesSaveReportAndRaiseTheirProperty()
  {
    var model = NewModel();
    var log = new PropertyLog(model);

    model.EditorConsole = true;
    model.ServerConsole = true;
    model.CloseAfterOpen = true;

    var saved = _rig.Saved()!;
    Assert.True((bool)saved["editorConsole"]!);
    Assert.True((bool)saved["serverConsole"]!);
    Assert.True((bool)saved["closeAfterOpen"]!);
    Assert.Equal(3, _updates.Count(update => update.Saved));
    Assert.Contains(nameof(SettingsViewModel.EditorConsole), log.Names);
    Assert.Contains(nameof(SettingsViewModel.ServerConsole), log.Names);
    Assert.Contains(nameof(SettingsViewModel.CloseAfterOpen), log.Names);

    _updates.Clear();
    model.EditorConsole = true;
    Assert.False(Assert.Single(_updates).Saved);
  }

  [Fact]
  public void AnInvalidPortIsReportedAndNeverSavedUntilItIsValidAgain()
  {
    var model = NewModel();
    var log = new PropertyLog(model);

    model.PortText = "abc";

    Assert.True(model.HasPortError);
    Assert.Contains("Enter a port", model.PortError);
    Assert.Null(_rig.Saved());
    Assert.Contains(nameof(SettingsViewModel.HasPortError), log.Names);

    model.PortText = "4242";

    Assert.False(model.HasPortError);
    Assert.Null(model.PortError);
    Assert.Equal(4242, (int)_rig.Saved()!["hostPort"]!);
  }

  [Fact]
  public void ChoosingAFullEngineFolderRelocatesItAndResetGoesBack()
  {
    _rig.AddEngine(LauncherRig.OtherDir);
    var model = NewModel();
    var log = new PropertyLog(model);
    Assert.True(model.HasEngineMissing);
    Assert.Null(model.EngineFoundText);

    model.EngineFolder = LauncherRig.OtherDir;

    var engine = Assert.Single(_updates).Engine;
    Assert.NotNull(engine);
    Assert.True(engine.IsComplete);
    Assert.Equal(LauncherRig.OtherDir, engine.Folder);
    Assert.False(model.HasEngineMissing);
    Assert.Null(model.EngineMissingText);
    Assert.Contains("Found", model.EngineFoundText);
    Assert.Contains(nameof(SettingsViewModel.EngineFoundText), log.Names);
    Assert.Contains(nameof(SettingsViewModel.EngineMissingText), log.Names);
    Assert.Contains(nameof(SettingsViewModel.HasEngineMissing), log.Names);

    model.ResetEngineCommand.Execute(null);

    Assert.Equal("", model.EngineFolder);
    Assert.False(_updates[^1].Engine!.IsComplete);
    Assert.True(model.HasEngineMissing);
  }

  [Fact]
  public void AnInvalidEngineFolderShowsAnErrorThatAValidOneClears()
  {
    var model = NewModel();

    model.EngineFolder = "bad\0folder";

    Assert.True(model.HasEngineError);
    Assert.Null(_updates[^1].Engine);

    model.EngineFolder = LauncherRig.OtherDir;

    Assert.False(model.HasEngineError);
    Assert.NotNull(_updates[^1].Engine);
  }

  [Fact]
  public void TheProjectsFolderShowsWhatTheEditorStored()
  {
    var model = NewModel();

    model.ProjectsFolder = "  /work/games  ";

    Assert.Equal("/work/games", model.ProjectsFolder);
    Assert.Equal("/work/games", (string)_rig.Saved()!["projectsDirectory"]!);

    model.ProjectsFolder = "";

    Assert.Equal(LauncherRig.DefaultProjects, model.ProjectsFolder);
  }

  [Fact]
  public void BrowsingTakesThePickedFolder()
  {
    var model = NewModel();
    model.PickEngineFolder = () => Task.FromResult<string?>(LauncherRig.OtherDir);
    model.PickProjectsFolder = () => Task.FromResult<string?>("/work/games");

    model.BrowseEngineCommand.Execute(null);
    model.BrowseProjectsCommand.Execute(null);

    Assert.Equal(LauncherRig.OtherDir, model.EngineFolder);
    Assert.Equal("/work/games", model.ProjectsFolder);
    Assert.Empty(_failures);
  }

  [Fact]
  public void ACancelledOrMissingPickerChangesNothing()
  {
    var model = NewModel();
    model.BrowseEngineCommand.Execute(null);
    Assert.Empty(_updates);

    model.PickEngineFolder = () => Task.FromResult<string?>(null);
    model.BrowseEngineCommand.Execute(null);

    Assert.Equal("", model.EngineFolder);
    Assert.Empty(_updates);

    model.PickEngineFolder = () => Task.FromResult<string?>(LauncherRig.OtherDir);
    model.BrowseEngineCommand.Execute(null);
    Assert.Equal(LauncherRig.OtherDir, model.EngineFolder);
  }

  [Fact]
  public void AFailingPickerIsReportedAndDoesNotBlockLaterBrowsing()
  {
    var model = NewModel();
    model.PickEngineFolder = () => throw new InvalidOperationException("no portal");

    model.BrowseEngineCommand.Execute(null);

    Assert.Contains("no portal", Assert.Single(_failures));
    Assert.Empty(_updates);

    model.PickEngineFolder = () => Task.FromResult<string?>(LauncherRig.OtherDir);
    model.BrowseEngineCommand.Execute(null);
    Assert.Equal(LauncherRig.OtherDir, model.EngineFolder);
  }

  [Fact]
  public void ASecondBrowseWhileOneIsOpenIsIgnoredUntilItFinishes()
  {
    NoContext.Run(() =>
    {
      var model = NewModel();
      var pending = new TaskCompletionSource<string?>();
      var picks = 0;
      model.PickEngineFolder = () =>
      {
        picks++;
        return pending.Task;
      };

      model.BrowseEngineCommand.Execute(null);
      model.BrowseEngineCommand.Execute(null);
      Assert.Equal(1, picks);

      pending.SetResult(LauncherRig.OtherDir);
      Assert.Equal(LauncherRig.OtherDir, model.EngineFolder);

      model.BrowseEngineCommand.Execute(null);
      Assert.Equal(2, picks);
    });
  }
}
