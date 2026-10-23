using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using ECS3DLauncher.Services;
using ECS3DLauncher.Shared.Models;
using ECS3DLauncher.Shell;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherProjectsTabTests
{
  private const string Alpha = "/work/alpha";
  private const string Beta = "/work/beta";
  private const string Gamma = "/games/gamma";
  private const string Legacy = "/work/old/legacy.json";

  private readonly LauncherRig _rig = new();
  private readonly FakeProcessRunner _runner = new();

  private void Seed(params (string Path, string Time, bool Pinned)[] entries)
  {
    var projects = entries.Select(entry => new JsonObject
    {
      ["path"] = entry.Path,
      ["name"] = entry.Path[(entry.Path.LastIndexOf('/') + 1)..].Replace(".json", ""),
      ["lastOpenedUtc"] = entry.Time,
      ["pinned"] = entry.Pinned,
    });
    var document = new JsonObject { ["version"] = 1, ["projects"] = new JsonArray(projects.Cast<JsonNode?>().ToArray()) };
    _rig.Files.Seed(_rig.RecentsPath, document.ToJsonString());
  }

  private MainWindowViewModel NewMain()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    return NewMainWithCurrentEngine();
  }

  private MainWindowViewModel NewMainWithCurrentEngine() =>
    new(
      _rig.NewEditor(),
      _rig.NewRecents(),
      _runner,
      new EarlyExitWatcher(ShellFixtures.UserData, TimeSpan.FromHours(1)),
      "1.2.3",
      action => action(),
      () => _rig.Time.Now,
      _ => null);

  private static string[] Names(MainWindowViewModel model) =>
    model.Projects.Projects.Select(project => project.Name).ToArray();

  private string[] StoredPaths() =>
    ((JsonArray)JsonNode.Parse(_rig.Files.TextOf(_rig.RecentsPath)!)!["projects"]!)
      .Select(node => (string)node!["path"]!)
      .ToArray();

  private static Project Card(MainWindowViewModel model, string name) =>
    model.Projects.Projects.Single(project => project.Name == name);

  [Fact]
  public void StoredProjectsShowPinnedFirstThenNewestFirst()
  {
    Seed((Alpha, "2026-10-18T10:00:00Z", false), (Beta, "2026-10-18T12:00:00Z", false), (Gamma, "2026-10-01T00:00:00Z", true));

    var model = NewMain();

    Assert.Equal(["gamma", "beta", "alpha"], Names(model));
    Assert.True(Card(model, "gamma").Pinned);
    Assert.False(Card(model, "beta").Pinned);
    Assert.Equal("9 hours ago", Card(model, "alpha").Edited);
    Assert.Equal("7 hours ago", Card(model, "beta").Edited);
    Assert.Equal("3", model.NavItems[0].Count);
  }

  [Fact]
  public void EditedTextFollowsTheInjectedClock()
  {
    Seed((Alpha, "2026-10-18T16:02:11Z", false), (Beta, "2026-09-01T00:00:00Z", false));

    var model = NewMain();

    Assert.Equal("3 hours ago", Card(model, "alpha").Edited);
    Assert.Equal("Sep 1, 2026", Card(model, "beta").Edited);

    _rig.Time.Now = _rig.Time.Now.AddDays(1);
    model.Refresh();

    Assert.Equal("Yesterday", Card(model, "alpha").Edited);
  }

  [Fact]
  public void SearchMatchesTheNameAndThePathButNotOtherText()
  {
    Seed((Alpha, "2026-10-18T10:00:00Z", false), (Gamma, "2026-10-18T09:00:00Z", false));
    var model = NewMain();

    model.SearchText = "ALPH";
    Assert.Equal(["alpha"], model.Projects.ProjectTiles.Skip(1).Select(tile => tile.Name).ToArray());

    model.SearchText = "/games/";
    Assert.Equal(["gamma"], model.Projects.ProjectTiles.Skip(1).Select(tile => tile.Name).ToArray());

    model.SearchText = "unrelated";
    Assert.Single(model.Projects.ProjectTiles);
  }

  [Fact]
  public void FirstRunShowsOnlyWhenNothingIsStoredNotWhenASearchHidesEverything()
  {
    var empty = NewMain();
    Assert.True(empty.Projects.IsEmpty);
    Assert.False(empty.Projects.ShowNoMatches);
    Assert.Equal("Welcome to ECS3D", empty.ViewTitle);

    Seed((Alpha, "2026-10-18T10:00:00Z", false));
    var stored = NewMainWithCurrentEngine();
    Assert.False(stored.Projects.IsEmpty);
    Assert.False(stored.Projects.ShowNoMatches);
    Assert.Equal("Recent Projects", stored.ViewTitle);

    stored.SearchText = "nothing matches";
    Assert.False(stored.Projects.IsEmpty);
    Assert.True(stored.Projects.ShowNoMatches);
    Assert.Equal("Recent Projects", stored.ViewTitle);
  }

  [Fact]
  public void TheTitleSwitchesWhenTheStoreGainsAndLosesProjects()
  {
    var model = NewMain();
    var log = new PropertyLog(model);
    Assert.Equal("Welcome to ECS3D", model.ViewTitle);

    Seed((Alpha, "2026-10-18T10:00:00Z", false));
    model.Refresh();
    Assert.Equal("Recent Projects", model.ViewTitle);
    Assert.Contains(nameof(MainWindowViewModel.ViewTitle), log.Names);

    model.SelectNavCommand.Execute("templates");
    Assert.Equal("Templates", model.ViewTitle);
  }

  [Fact]
  public void OpeningRecordsTheProjectAtTheFrontAndStartsTheEditor()
  {
    Seed((Alpha, "2026-10-18T10:00:00Z", false), (Beta, "2026-10-18T12:00:00Z", false));
    var model = NewMain();
    Assert.Equal(["beta", "alpha"], Names(model));
    _rig.Time.Now = new DateTimeOffset(2026, 10, 18, 20, 0, 0, TimeSpan.Zero);

    model.OpenProjectCommand.Execute(Card(model, "alpha"));

    Assert.Equal([Alpha, Beta], StoredPaths());
    var saved = (JsonObject)JsonNode.Parse(_rig.Files.TextOf(_rig.RecentsPath)!)!;
    Assert.Equal("2026-10-18T20:00:00Z", (string)saved["projects"]![0]!["lastOpenedUtc"]!);
    Assert.Equal(["alpha", "beta"], Names(model));
    var launch = Assert.Single(_runner.Launches);
    Assert.EndsWith("ECS3DEditor", launch.FileName);
    Assert.Equal(new[] { "--project", Alpha }, launch.Arguments.ToArray());
  }

  [Fact]
  public void AFailedSaveToastsButTheProjectStillOpens()
  {
    Seed((Alpha, "2026-10-18T10:00:00Z", false));
    var model = NewMain();
    Assert.False(model.HasToast);
    _rig.Files.FailWrites = true;

    model.OpenProjectCommand.Execute(Card(model, "alpha"));

    Assert.True(model.HasToast);
    Assert.Contains(_rig.RecentsPath, model.ToastMessage);
    Assert.Single(_runner.Launches);
  }

  [Fact]
  public void ALegacyFileOpensWithItsFilePath()
  {
    Seed((Legacy, "2026-10-18T10:00:00Z", false));
    var model = NewMain();

    model.OpenProjectCommand.Execute(Card(model, "legacy"));

    Assert.Equal(new[] { "--project", Legacy }, Assert.Single(_runner.Launches).Arguments.ToArray());
    Assert.Null(Card(model, "legacy").Thumbnail);
  }

  [Fact]
  public void PlayStartsTheClientWithTheProject()
  {
    Seed((Alpha, "2026-10-18T10:00:00Z", false));
    var model = NewMain();
    var alpha = Card(model, "alpha");
    Assert.True(model.PlayProjectCommand.CanExecute(alpha));
    Assert.Null(model.Projects.PlayTooltip);

    model.PlayProjectCommand.Execute(alpha);

    var launch = Assert.Single(_runner.Launches);
    Assert.EndsWith("ECS3DClient", launch.FileName);
    Assert.Equal(new[] { "--project", Alpha }, launch.Arguments.ToArray());
  }

  [Fact]
  public void PlayIsDisabledWithoutTheClientWhileOpenStaysEnabled()
  {
    Seed((Alpha, "2026-10-18T10:00:00Z", false));
    _rig.AddEngine(LauncherRig.LauncherDir, EngineExecutable.Client);
    var model = NewMainWithCurrentEngine();
    var alpha = Card(model, "alpha");

    Assert.False(model.PlayProjectCommand.CanExecute(alpha));
    Assert.NotNull(model.Projects.PlayTooltip);
    Assert.True(model.OpenProjectCommand.CanExecute(alpha));

    model.PlayProjectCommand.Execute(alpha);
    Assert.Empty(_runner.Launches);
  }

  [Fact]
  public void PinningAndUnpinningPersistAndReorder()
  {
    Seed((Alpha, "2026-10-18T10:00:00Z", false), (Beta, "2026-10-18T12:00:00Z", false));
    var model = NewMain();
    Assert.Equal(["beta", "alpha"], Names(model));

    model.TogglePinCommand.Execute(Card(model, "alpha"));

    Assert.Equal(["alpha", "beta"], Names(model));
    Assert.True(Card(model, "alpha").Pinned);
    Assert.True((bool)JsonNode.Parse(_rig.Files.TextOf(_rig.RecentsPath)!)!["projects"]![0]!["pinned"]!);

    model.TogglePinCommand.Execute(Card(model, "alpha"));

    Assert.Equal(["beta", "alpha"], Names(model));
    Assert.False(Card(model, "alpha").Pinned);
    Assert.DoesNotContain("\"pinned\": true", _rig.Files.TextOf(_rig.RecentsPath));
  }

  [Fact]
  public void RemovingDropsTheEntryFromTheFileAndLeavesTheProjectFilesAlone()
  {
    Seed((Alpha, "2026-10-18T10:00:00Z", false), (Beta, "2026-10-18T12:00:00Z", false));
    _rig.Files.AddDirectory(Alpha);
    _rig.Files.Seed(Alpha + "/project.ecs3d", "{}");
    var model = NewMain();

    model.RemoveProjectCommand.Execute(Card(model, "alpha"));

    Assert.Equal(["beta"], Names(model));
    Assert.Equal([Beta], StoredPaths());
    Assert.Equal("1", model.NavItems[0].Count);
    Assert.True(_rig.Files.DirectoryExists(Alpha));
    Assert.True(_rig.Files.FileExists(Alpha + "/project.ecs3d"));
  }

  [Theory]
  [InlineData(Alpha, Alpha)]
  [InlineData(Legacy, "/work/old")]
  public void ShowInFolderOpensTheProjectFolderOrTheOneHoldingALegacyFile(string path, string expected)
  {
    Seed((path, "2026-10-18T10:00:00Z", false));
    var model = NewMain();
    var shown = new List<string>();
    model.ShowFolder = folder =>
    {
      shown.Add(folder);
      return Task.FromResult(true);
    };

    model.ShowInFolderCommand.Execute(model.Projects.Projects[0]);

    Assert.Equal([expected], shown);
    Assert.False(model.HasToast);
  }

  [Fact]
  public void AFailedShowInFolderToasts()
  {
    Seed((Alpha, "2026-10-18T10:00:00Z", false));
    var model = NewMain();
    model.ShowFolder = _ => Task.FromResult(false);

    model.ShowInFolderCommand.Execute(model.Projects.Projects[0]);

    Assert.True(model.HasToast);
    Assert.Contains(Alpha, model.ToastMessage);
  }

  [Fact]
  public void RefreshPicksUpAnEntryWrittenAfterStartupAndKeepsTheSearch()
  {
    Seed((Alpha, "2026-10-18T10:00:00Z", false));
    var model = NewMain();
    model.SearchText = "beta";
    Assert.Single(model.Projects.ProjectTiles);

    Seed((Alpha, "2026-10-18T10:00:00Z", false), (Beta, "2026-10-18T12:00:00Z", false));
    model.Refresh();

    Assert.Equal(["beta", "alpha"], Names(model));
    Assert.Equal("beta", model.Projects.SearchText);
    Assert.Equal(["beta"], model.Projects.ProjectTiles.Skip(1).Select(tile => tile.Name).ToArray());
  }

  [Fact]
  public void BrowseSamplesSwitchesToTheSamplesTab()
  {
    var model = NewMain();
    Assert.Same(model.Projects, model.CurrentTab);

    model.Projects.BrowseSamplesCommand.Execute(null);

    Assert.Same(model.Samples, model.CurrentTab);
    Assert.Equal("samples", model.ActiveNav);
  }

  [Fact]
  public void FirstRunNewProjectSwitchesToTemplates()
  {
    var model = NewMain();

    model.Projects.NewProjectCommand.Execute(null);

    Assert.Same(model.Templates, model.CurrentTab);
  }
}
