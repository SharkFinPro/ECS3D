using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json.Nodes;
using System.Threading;
using System.Threading.Tasks;
using ECS3DLauncher.Services;
using ECS3DLauncher.Shared.Models;
using ECS3DLauncher.Shell;
using Xunit;

namespace ECS3DManagedTests;

// Every path exists, so a test that is not about missing projects sees none.
internal sealed class PresentFileSystem : IFileSystem
{
  public bool FileExists(string path) => true;

  public bool DirectoryExists(string path) => true;

  public IReadOnlyList<string> EnumerateDirectories(string path) => [];

  public string ReadAllText(string path) => throw new NotSupportedException();

  public void WriteAllTextAtomic(string path, string contents) => throw new NotSupportedException();

  public void CreateDirectory(string path) => throw new NotSupportedException();
}

// Only the paths added exist, and they can be removed again, as when a folder is deleted or restored.
internal sealed class ProjectTreeFileSystem : IFileSystem
{
  private readonly HashSet<string> _files = [];
  private readonly HashSet<string> _directories = [];

  public void AddProject(string folder)
  {
    _directories.Add(folder);
    _files.Add(RecentProjectPaths.ProjectFileIn(folder));
  }

  public void RemoveProject(string folder)
  {
    _directories.Remove(folder);
    _files.Remove(RecentProjectPaths.ProjectFileIn(folder));
  }

  public void AddFile(string path) => _files.Add(path);

  public void AddFolder(string path) => _directories.Add(path);

  public bool FileExists(string path) => _files.Contains(path);

  public bool DirectoryExists(string path) => _directories.Contains(path);

  public IReadOnlyList<string> EnumerateDirectories(string path) => [];

  public string ReadAllText(string path) => throw new NotSupportedException();

  public void WriteAllTextAtomic(string path, string contents) => throw new NotSupportedException();

  public void CreateDirectory(string path) => throw new NotSupportedException();
}

public class LauncherProjectsMissingTests
{
  private const string Alpha = "/work/alpha";
  private const string Beta = "/work/beta";
  private const string Legacy = "/work/old/legacy.json";
  private const string Moved = "/moved/alpha2";
  private const string InvalidChoice = "Choose a project folder, a project.ecs3d file or a .json project file.";

  private readonly LauncherRig _rig = new();
  private readonly RecordingProcessRunner _runner = new();
  private readonly ProjectTreeFileSystem _tree = new();
  private int _posts;

  private void Seed(params (string Path, bool Pinned)[] entries)
  {
    var projects = entries.Select((entry, index) => new JsonObject
    {
      ["path"] = entry.Path,
      ["name"] = entry.Path[(entry.Path.LastIndexOf('/') + 1)..].Replace(".json", ""),
      ["lastOpenedUtc"] = $"2026-10-18T1{index}:00:00Z",
      ["pinned"] = entry.Pinned,
    });
    var document = new JsonObject { ["version"] = 1, ["projects"] = new JsonArray(projects.Cast<JsonNode?>().ToArray()) };
    _rig.Files.Seed(_rig.RecentsPath, document.ToJsonString());
  }

  private MainWindowViewModel NewMain(Action<Action>? runInBackground = null)
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    return new MainWindowViewModel(
      _rig.NewEditor(),
      _rig.NewRecents(),
      _runner,
      new EarlyExitWatcher(ShellFixtures.UserData, TimeSpan.FromHours(1)),
      _rig.NewCatalog(),
      _ => null,
      "1.2.3",
      action =>
      {
        action();
        Interlocked.Increment(ref _posts);
      },
      () => _rig.Time.Now,
      _tree,
      runInBackground ?? (work => work()));
  }

  private static Project Card(MainWindowViewModel model, string name) =>
    model.Projects.Projects.Single(project => project.Name == name);

  private static Func<Task<string?>> Picks(string? path) => () => Task.FromResult(path);

  private JsonNode[] Stored() =>
    ((JsonArray)JsonNode.Parse(_rig.Files.TextOf(_rig.RecentsPath)!)!["projects"]!).Select(node => node!).ToArray();

  [Fact]
  public void AFolderWithoutItsProjectFileAndAGoneLegacyFileAreMissingWhileAPresentOneIsNot()
  {
    _tree.AddProject(Alpha);
    _tree.AddFolder(Beta);
    Seed((Alpha, false), (Beta, false), (Legacy, false));

    var model = NewMain();

    Assert.False(Card(model, "alpha").IsMissing);
    Assert.True(Card(model, "beta").IsMissing);
    Assert.True(Card(model, "legacy").IsMissing);

    _tree.AddFile(Legacy);
    model.Refresh();
    Assert.False(Card(model, "legacy").IsMissing);
  }

  [Fact]
  public void OpenPlayAndShowInFolderAreUnavailableForAMissingCardButAvailableForAPresentOne()
  {
    _tree.AddProject(Alpha);
    Seed((Alpha, false), (Beta, false));
    var model = NewMain();
    var present = Card(model, "alpha");
    var missing = Card(model, "beta");

    Assert.True(model.OpenProjectCommand.CanExecute(present));
    Assert.True(model.PlayProjectCommand.CanExecute(present));
    Assert.True(model.ShowInFolderCommand.CanExecute(present));
    Assert.False(model.OpenProjectCommand.CanExecute(missing));
    Assert.False(model.PlayProjectCommand.CanExecute(missing));
    Assert.False(model.ShowInFolderCommand.CanExecute(missing));

    model.OpenProjectCommand.Execute(missing);
    Assert.Empty(_runner.Launches);

    model.OpenProjectCommand.Execute(present);
    Assert.Single(_runner.Launches);
  }

  [Fact]
  public void RefreshFlipsTheFlagWhenTheFolderDisappearsOrComesBackWithTheStoredEntriesUnchanged()
  {
    _tree.AddProject(Alpha);
    _tree.AddProject(Beta);
    Seed((Alpha, false), (Beta, false));
    var model = NewMain();
    var before = _rig.Files.TextOf(_rig.RecentsPath);

    _tree.RemoveProject(Alpha);
    model.Refresh();
    Assert.True(Card(model, "alpha").IsMissing);
    Assert.False(Card(model, "beta").IsMissing);

    _tree.AddProject(Alpha);
    model.Refresh();
    Assert.False(Card(model, "alpha").IsMissing);
    Assert.Equal(before, _rig.Files.TextOf(_rig.RecentsPath));
  }

  [Fact]
  public void LocatingWithAValidFolderRelocatesTheEntryAndKeepsItsPin()
  {
    _tree.AddProject(Moved);
    Seed((Alpha, true), (Beta, false));
    var model = NewMain();
    Assert.True(Card(model, "alpha").IsMissing);
    model.PickProjectFolder = Picks(Moved);

    model.LocateProjectCommand.Execute(Card(model, "alpha"));

    var entry = Stored().Single(node => (string)node["path"]! == Moved);
    Assert.Equal("alpha2", (string)entry["name"]!);
    Assert.True((bool)entry["pinned"]!);
    Assert.Equal(2, Stored().Length);
    Assert.DoesNotContain(Stored(), node => (string)node["path"]! == Alpha);
    var card = Card(model, "alpha2");
    Assert.False(card.IsMissing);
    Assert.True(card.Pinned);
    Assert.False(model.HasToast);
  }

  [Fact]
  public void LocatingWithAFolderThatIsNotAProjectToastsAndLeavesTheFileAlone()
  {
    _tree.AddFolder("/empty");
    _tree.AddProject(Moved);
    Seed((Alpha, false));
    var model = NewMain();
    var before = _rig.Files.TextOf(_rig.RecentsPath);
    model.PickProjectFolder = Picks("/empty");

    model.LocateProjectCommand.Execute(Card(model, "alpha"));

    Assert.True(model.HasToast);
    Assert.Contains("project.ecs3d", model.ToastMessage);
    Assert.Equal(before, _rig.Files.TextOf(_rig.RecentsPath));
    Assert.True(Card(model, "alpha").IsMissing);

    model.PickProjectFolder = Picks(Moved);
    model.LocateProjectCommand.Execute(Card(model, "alpha"));
    Assert.Equal(Moved, (string)Stored().Single()["path"]!);
  }

  [Fact]
  public void ACancelledLocateChangesNothing()
  {
    _tree.AddProject(Moved);
    Seed((Alpha, false));
    var model = NewMain();
    var before = _rig.Files.TextOf(_rig.RecentsPath);
    model.PickProjectFolder = Picks(null);

    model.LocateProjectCommand.Execute(Card(model, "alpha"));

    Assert.Equal(before, _rig.Files.TextOf(_rig.RecentsPath));
    Assert.False(model.HasToast);
    Assert.True(Card(model, "alpha").IsMissing);

    model.PickProjectFolder = Picks(Moved);
    model.LocateProjectCommand.Execute(Card(model, "alpha"));
    Assert.NotEqual(before, _rig.Files.TextOf(_rig.RecentsPath));
  }

  [Fact]
  public void LocatingOntoAListedProjectMergesTheTwoEntries()
  {
    _tree.AddProject(Beta);
    Seed((Alpha, true), (Beta, false));
    var model = NewMain();
    model.PickProjectFolder = Picks(Beta);

    model.LocateProjectCommand.Execute(Card(model, "alpha"));

    var entry = Assert.Single(Stored());
    Assert.Equal(Beta, (string)entry["path"]!);
    Assert.True((bool)entry["pinned"]!);
  }

  [Fact]
  public void OpenExistingRecordsAProjectFileFirstAndOpensItsFolder()
  {
    _tree.AddProject("/new/proj");
    _tree.AddProject(Alpha);
    Seed((Alpha, false));
    var model = NewMain();
    model.PickProjectFile = Picks("/new/proj/project.ecs3d");

    model.OpenExistingCommand.Execute(null);

    Assert.Equal(["/new/proj", Alpha], Stored().Select(node => (string)node["path"]!).ToArray());
    var launch = Assert.Single(_runner.Launches);
    Assert.EndsWith("ECS3DEditor", launch.FileName);
    Assert.Equal(new[] { "--project", "/new/proj" }, launch.Arguments.ToArray());
    Assert.False(model.HasToast);
  }

  [Fact]
  public void OpenExistingRecordsALegacyJsonFileAndOpensItByItsFilePath()
  {
    _tree.AddFile("/new/old/game.json");
    var model = NewMain();
    model.PickProjectFile = Picks("/new/old/game.json");

    model.OpenExistingCommand.Execute(null);

    Assert.Equal("/new/old/game.json", (string)Assert.Single(Stored())["path"]!);
    Assert.Equal(new[] { "--project", "/new/old/game.json" }, Assert.Single(_runner.Launches).Arguments.ToArray());
    Assert.Equal("game", Card(model, "game").Name);
  }

  [Fact]
  public void OpenExistingWithAnInvalidFileShowsTheErrorAndRecordsNothing()
  {
    _tree.AddFile("/new/notes.txt");
    _tree.AddProject("/new/proj");
    Seed((Alpha, false));
    var model = NewMain();
    var before = _rig.Files.TextOf(_rig.RecentsPath);
    model.PickProjectFile = Picks("/new/notes.txt");

    model.OpenExistingCommand.Execute(null);

    Assert.Equal(InvalidChoice, model.ToastMessage);
    Assert.Equal(before, _rig.Files.TextOf(_rig.RecentsPath));
    Assert.Empty(_runner.Launches);

    model.DismissToastCommand.Execute(null);
    model.PickProjectFile = Picks("/new/proj/project.ecs3d");
    model.OpenExistingCommand.Execute(null);
    Assert.False(model.HasToast);
    Assert.Single(_runner.Launches);
  }

  [Fact]
  public void ACancelledOpenExistingDoesNothing()
  {
    _tree.AddProject("/new/proj");
    var model = NewMain();
    model.PickProjectFile = Picks(null);

    model.OpenExistingCommand.Execute(null);

    Assert.Null(_rig.Files.TextOf(_rig.RecentsPath));
    Assert.Empty(_runner.Launches);
    Assert.False(model.HasToast);

    model.PickProjectFile = Picks("/new/proj/project.ecs3d");
    model.OpenExistingCommand.Execute(null);
    Assert.Single(_runner.Launches);
  }

  [Fact]
  public void OpenExistingIsOnlyShownOnTheProjectsTab()
  {
    var model = NewMain();
    Assert.True(model.ShowOpenExisting);

    model.SelectNavCommand.Execute("templates");
    Assert.False(model.ShowOpenExisting);

    model.SelectNavCommand.Execute("projects");
    Assert.True(model.ShowOpenExisting);
  }

  [Fact]
  public void TheInEditorBadgeShowsWhileTheEditorRunsAndClearsWhenItExits()
  {
    _tree.AddProject(Alpha);
    _tree.AddProject(Beta);
    Seed((Alpha, false), (Beta, false));
    var model = NewMain();
    Assert.False(Card(model, "alpha").IsInEditor);

    model.OpenProjectCommand.Execute(Card(model, "alpha"));

    Assert.True(Card(model, "alpha").IsInEditor);
    Assert.False(Card(model, "beta").IsInEditor);

    ExitAndWait(0, 0);

    Assert.False(Card(model, "alpha").IsInEditor);
    Assert.False(_runner.Processes[0].Killed);
  }

  [Fact]
  public void TheBadgeStaysUntilTheLastEditorOfTheProjectExits()
  {
    _tree.AddProject(Alpha);
    Seed((Alpha, false));
    var model = NewMain();

    model.OpenProjectCommand.Execute(Card(model, "alpha"));
    model.OpenProjectCommand.Execute(Card(model, "alpha"));
    Assert.Equal(2, _runner.Processes.Count);

    ExitAndWait(0, 0);
    Assert.True(Card(model, "alpha").IsInEditor);

    ExitAndWait(1, 0);
    Assert.False(Card(model, "alpha").IsInEditor);
  }

  [Fact]
  public void PlayingAProjectDoesNotShowTheBadge()
  {
    _tree.AddProject(Alpha);
    _tree.AddProject(Beta);
    Seed((Alpha, false), (Beta, false));
    var model = NewMain();

    model.PlayProjectCommand.Execute(Card(model, "beta"));
    Assert.Single(_runner.Launches);
    Assert.False(Card(model, "beta").IsInEditor);

    model.OpenProjectCommand.Execute(Card(model, "alpha"));
    Assert.True(Card(model, "alpha").IsInEditor);
    Assert.False(Card(model, "beta").IsInEditor);
  }

  [Fact]
  public void APickedItemWithNoLocalPathToastsInsteadOfCancelling()
  {
    _tree.AddProject(Moved);
    Seed((Alpha, false));
    var model = NewMain();
    model.PickProjectFolder = Picks(string.Empty);
    model.PickProjectFile = Picks(string.Empty);

    model.LocateProjectCommand.Execute(Card(model, "alpha"));
    Assert.Equal("Choose a folder on this computer.", model.ToastMessage);

    model.OpenExistingCommand.Execute(null);
    Assert.Equal("Choose a file on this computer.", model.ToastMessage);
    Assert.Empty(_runner.Launches);

    model.PickProjectFolder = Picks(Moved);
    model.LocateProjectCommand.Execute(Card(model, "alpha"));
    Assert.Equal(Moved, (string)Stored().Single()["path"]!);
  }

  [Fact]
  public void TheDiskChecksRunInTheBackgroundAndAreAppliedWhenTheyFinish()
  {
    _tree.AddProject(Alpha);
    Seed((Alpha, false));
    var queued = new List<Action>();
    var model = NewMain(queued.Add);
    Assert.Single(queued);
    Assert.False(Card(model, "alpha").IsMissing);

    _tree.RemoveProject(Alpha);
    model.Refresh();
    Assert.False(Card(model, "alpha").IsMissing);

    foreach (var work in queued.ToList())
    {
      work();
    }

    Assert.True(Card(model, "alpha").IsMissing);
  }

  // Waits for the post that follows the exit; the tracker finishes on another thread.
  private void ExitAndWait(int index, int exitCode)
  {
    var before = Volatile.Read(ref _posts);
    _runner.Processes[index].Exit(exitCode);
    Assert.True(SpinWait.SpinUntil(() => Volatile.Read(ref _posts) > before, TimeSpan.FromSeconds(5)));
  }
}
