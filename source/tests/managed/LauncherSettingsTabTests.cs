using System;
using System.IO;
using System.Linq;
using System.Text.Json.Nodes;
using ECS3DLauncher.Services;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherSettingsTabTests
{
  private static readonly string LauncherDir = Path.GetFullPath(Path.Combine("tab-launcher", "dir"));
  private static readonly string OtherDir = Path.GetFullPath(Path.Combine("tab-other", "bin"));
  private static readonly string DocumentsProjects = Path.Combine("/docs", "ECS3D Projects");

  private readonly FakeFileSystem _files = new();
  private readonly RecordingLauncherLog _log = new();
  private readonly UserDataDirectory _userData = ShellFixtures.UserData;

  private string SettingsPath => _userData.SettingsFile();

  private JsonObject Saved() => (JsonObject)JsonNode.Parse(_files.TextOf(SettingsPath)!)!;

  private void AddEngine(string folder, params EngineExecutable[] skip)
  {
    foreach (var executable in Enum.GetValues<EngineExecutable>().Where(item => !skip.Contains(item)))
    {
      _files.Seed(Path.Combine(folder, EngineExecutableNames.FileName(executable, HostOs.Linux)), "");
    }
  }

  private SettingsEditor NewEditor()
  {
    var store = new LauncherSettingsStore(_files, _log, _userData, () => "/docs");
    return new SettingsEditor(store, new EngineLocator(_files, HostOs.Linux, LauncherDir));
  }

  [Fact]
  public void EachFieldChangeIsSavedAtOnce()
  {
    var editor = NewEditor();
    AddEngine(OtherDir);

    Assert.True(editor.SetEngineDirectory(OtherDir).Saved);
    Assert.Equal(OtherDir, (string)Saved()["engineDirectory"]!);
    Assert.True(editor.SetProjectsDirectory("/work/games").Saved);
    Assert.Equal("/work/games", (string)Saved()["projectsDirectory"]!);
    Assert.True(editor.SetEditorConsole(true).Saved);
    Assert.True((bool)Saved()["editorConsole"]!);
    Assert.True(editor.SetServerConsole(true).Saved);
    Assert.True((bool)Saved()["serverConsole"]!);
    Assert.True(editor.SetCloseAfterOpen(true).Saved);
    Assert.True((bool)Saved()["closeAfterOpen"]!);
    Assert.True(editor.SetHostPort(" 4242 ").Saved);
    Assert.Equal(4242, (int)Saved()["hostPort"]!);
    Assert.True(editor.SetEditorConsole(false).Saved);
    Assert.False((bool)Saved()["editorConsole"]!);
    Assert.Empty(_log.Warnings);
  }

  [Fact]
  public void SavedChangesComeBackOnTheNextLoad()
  {
    AddEngine(OtherDir);
    var editor = NewEditor();
    editor.SetEngineDirectory(OtherDir);
    editor.SetServerConsole(true);
    editor.SetHostPort("4100");
    editor.SetProjectsView(false);

    var reloaded = NewEditor();

    Assert.Equal(editor.Current, reloaded.Current);
    Assert.Equal(OtherDir, reloaded.Engine.Folder);
    Assert.True(reloaded.Engine.IsComplete);
  }

  [Fact]
  public void UnchangedValuesAreNotWritten()
  {
    var editor = NewEditor();

    var update = editor.SetEditorConsole(false);

    Assert.False(update.Saved);
    Assert.False(update.SaveFailed);
    Assert.Null(_files.TextOf(SettingsPath));
  }

  [Theory]
  [InlineData("0")]
  [InlineData("65536")]
  [InlineData("-1")]
  [InlineData("abc")]
  [InlineData("")]
  [InlineData(null)]
  public void InvalidPortIsReportedAndNeverSaved(string? text)
  {
    var editor = NewEditor();
    Assert.True(editor.SetHostPort("4000").Saved);

    var update = editor.SetHostPort(text);

    Assert.False(update.Saved);
    Assert.NotNull(editor.PortError);
    Assert.Equal(4000, editor.Current.HostPort);
    Assert.Equal(4000, (int)Saved()["hostPort"]!);
  }

  [Fact]
  public void ValidPortClearsTheError()
  {
    var editor = NewEditor();
    editor.SetHostPort("nope");
    Assert.NotNull(editor.PortError);

    var update = editor.SetHostPort("65535");

    Assert.True(update.Saved);
    Assert.Null(editor.PortError);
    Assert.Equal(65535, editor.Current.HostPort);
  }

  [Fact]
  public void ChangingTheEngineFolderRelocatesAndReportsFoundOrMissing()
  {
    AddEngine(OtherDir);
    var editor = NewEditor();
    Assert.False(editor.Engine.IsComplete);
    Assert.NotNull(editor.EngineMissingText);
    Assert.Null(editor.EngineFoundText);

    var found = editor.SetEngineDirectory(OtherDir);

    Assert.True(found.Engine!.IsComplete);
    Assert.Same(found.Engine, editor.Engine);
    Assert.Null(editor.EngineMissingText);
    Assert.Contains("ECS3DEditor", editor.EngineFoundText);
    Assert.Contains(OtherDir, editor.EngineFoundText);

    var partial = Path.GetFullPath(Path.Combine("tab-partial", "bin"));
    AddEngine(partial, EngineExecutable.Server);
    var missing = editor.SetEngineDirectory(partial);

    Assert.False(missing.Engine!.IsComplete);
    Assert.Contains("ECS3DServer", editor.EngineMissingText);
    Assert.Contains(partial, editor.EngineMissingText);
    Assert.Contains("ECS3DEditor", editor.EngineFoundText);
  }

  [Fact]
  public void ResetToDefaultClearsTheOverrideAndUsesTheLauncherFolder()
  {
    AddEngine(OtherDir);
    var editor = NewEditor();
    editor.SetEngineDirectory(OtherDir);
    Assert.Equal(OtherDir, editor.Engine.Folder);

    var update = editor.ResetEngineDirectory();

    Assert.True(update.Saved);
    Assert.Equal(LauncherDir, update.Engine!.Folder);
    Assert.Null(editor.Current.EngineDirectory);
    Assert.Null(Saved()["engineDirectory"]);
    Assert.Equal("", editor.EngineFolderText);
  }

  [Fact]
  public void AnInvalidEngineFolderIsReportedAndKeepsTheLocatedEngine()
  {
    AddEngine(OtherDir);
    var editor = NewEditor();
    editor.SetEngineDirectory(OtherDir);

    var update = editor.SetEngineDirectory("bad\0folder");

    Assert.False(update.Saved);
    Assert.Null(update.Engine);
    Assert.NotNull(editor.EngineError);
    Assert.Equal(OtherDir, editor.Engine.Folder);
    Assert.Equal(OtherDir, (string)Saved()["engineDirectory"]!);
  }

  [Fact]
  public void SavedEngineFolderIsUsedAtStartup()
  {
    AddEngine(OtherDir);
    _files.Seed(SettingsPath, new JsonObject { ["engineDirectory"] = OtherDir }.ToJsonString());

    var editor = NewEditor();

    Assert.Equal(OtherDir, editor.Engine.Folder);
    Assert.True(editor.Engine.IsComplete);
    Assert.Equal(OtherDir, editor.EngineFolderText);
  }

  [Fact]
  public void EmptyProjectsLocationFallsBackToTheDefault()
  {
    var editor = NewEditor();
    editor.SetProjectsDirectory("/work/games");

    var update = editor.SetProjectsDirectory("  ");

    Assert.True(update.Saved);
    Assert.Equal(DocumentsProjects, editor.Current.ProjectsDirectory);
  }

  [Fact]
  public void FailedSaveIsReportedAndTheChangeStaysApplied()
  {
    var editor = NewEditor();
    _files.FailWrites = true;

    var update = editor.SetEditorConsole(true);

    Assert.True(update.SaveFailed);
    Assert.False(update.Saved);
    Assert.True(editor.Current.EditorConsole);
  }

  [Fact]
  public void LastJoinShowsADashUntilOneIsStored()
  {
    Assert.Equal("-", NewEditor().LastJoinText);

    _files.Seed(SettingsPath, """{ "lastJoin": { "address": "192.0.2.24", "port": 3001 } }""");

    Assert.Equal("192.0.2.24:3001", NewEditor().LastJoinText);
  }

  [Fact]
  public void ProjectsViewChoiceRoundTrips()
  {
    var editor = NewEditor();
    Assert.Equal(LauncherSettings.GridView, editor.Current.ProjectsView);

    Assert.True(editor.SetProjectsView(false).Saved);
    Assert.Equal("list", (string)Saved()["projectsView"]!);
    Assert.Equal(LauncherSettings.ListView, NewEditor().Current.ProjectsView);

    Assert.True(editor.SetProjectsView(true).Saved);
    Assert.Equal(LauncherSettings.GridView, NewEditor().Current.ProjectsView);
  }

  [Fact]
  public void SettingsLaunchOptionsReachTheEditorArgumentsInOrder()
  {
    AddEngine(LauncherDir);
    var editor = NewEditor();
    var runner = new FakeProcessRunner();
    var launcher = new EngineLauncher(runner, editor.Engine);

    launcher.OpenProject("/p", editor.Current.ToLaunchOptions());
    editor.SetServerConsole(true);
    launcher.OpenProject("/p", editor.Current.ToLaunchOptions());
    editor.SetEditorConsole(true);
    launcher.OpenProject("/p", editor.Current.ToLaunchOptions());
    launcher.ExploreEngine(editor.Current.ToLaunchOptions());

    Assert.Equal(new[] { "--project", "/p" }, runner.Launches[0].Arguments.ToArray());
    Assert.Equal(new[] { "--project", "/p", "--server-console" }, runner.Launches[1].Arguments.ToArray());
    Assert.Equal(
      new[] { "--project", "/p", "--console", "--server-console" }, runner.Launches[2].Arguments.ToArray());
    Assert.Equal(new[] { "--console", "--server-console" }, runner.Launches[3].Arguments.ToArray());
  }
}
