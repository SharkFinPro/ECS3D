using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using Avalonia.Media.Imaging;
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

  private MainWindowViewModel NewMainWithCurrentEngine(Func<string, Bitmap?>? loadThumbnail = null) =>
    new(
      _rig.NewEditor(),
      _rig.NewRecents(),
      _runner,
      new EarlyExitWatcher(ShellFixtures.UserData, TimeSpan.FromHours(1)),
      _rig.NewCatalog(),
      loadThumbnail ?? (_ => null),
      "1.2.3",
      action => action(),
      () => _rig.Time.Now);

  private static string[] Names(MainWindowViewModel model) =>
    model.Projects.Projects.Select(project => project.Name).ToArray();

  private string[] StoredPaths() =>
    ((JsonArray)JsonNode.Parse(_rig.Files.TextOf(_rig.RecentsPath)!)!["projects"]!)
      .Select(node => (string)node!["path"]!)
      .ToArray();

  private JsonNode StoredEntry(string path) =>
    ((JsonArray)JsonNode.Parse(_rig.Files.TextOf(_rig.RecentsPath)!)!["projects"]!)
      .Single(node => (string)node!["path"]! == path)!;

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
    Assert.Equal("2026-10-18T20:00:00Z", (string)StoredEntry(Alpha)["lastOpenedUtc"]!);
    Assert.Equal("2026-10-18T12:00:00Z", (string)StoredEntry(Beta)["lastOpenedUtc"]!);
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
    Assert.True((bool)StoredEntry(Alpha)["pinned"]!);

    model.TogglePinCommand.Execute(Card(model, "alpha"));

    Assert.Equal(["beta", "alpha"], Names(model));
    Assert.False(Card(model, "alpha").Pinned);
    Assert.False(StoredEntry(Alpha)["pinned"]?.GetValue<bool>() ?? false);
  }

  [Fact]
  public void RemovingDropsOnlyThatEntryFromTheFile()
  {
    Seed((Alpha, "2026-10-18T10:00:00Z", false), (Beta, "2026-10-18T12:00:00Z", false));
    var model = NewMain();

    model.RemoveProjectCommand.Execute(Card(model, "alpha"));

    Assert.Equal(["beta"], Names(model));
    Assert.Equal([Beta], StoredPaths());
    Assert.Equal("1", model.NavItems[0].Count);
  }

  [Theory]
  [InlineData(Alpha, Alpha)]
  [InlineData(Legacy, "/work/old")]
  [InlineData("C:/old.json", "C:/")]
  [InlineData("/top.json", "/")]
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

  private MainWindowViewModel NewMain(List<string> requested)
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    return NewMainWithCurrentEngine(path =>
    {
      requested.Add(path);
      return null;
    });
  }

  [Fact]
  public void ThumbnailsAreRequestedFromTheProjectFolderAndNeverForALegacyFile()
  {
    Seed((Alpha, "2026-10-18T10:00:00Z", false), (Legacy, "2026-10-18T09:00:00Z", false));
    var requested = new List<string>();

    NewMain(requested);

    Assert.Equal([Path.Combine(Alpha, "thumbnail.png")], requested);
  }

  [Fact]
  public void RefreshingAnUnchangedListDoesNotDecodeThumbnailsAgain()
  {
    Seed((Alpha, "2026-10-18T10:00:00Z", false));
    var requested = new List<string>();
    var model = NewMain(requested);
    Assert.Single(requested);

    model.Refresh();
    Assert.Single(requested);

    Seed((Alpha, "2026-10-18T10:00:00Z", false), (Beta, "2026-10-18T12:00:00Z", false));
    model.Refresh();
    Assert.Equal(3, requested.Count);
  }

  [Fact]
  public void ATogglePinUsesTheFileAsItIsNowAndKeepsEntriesAnotherProgramAdded()
  {
    Seed((Alpha, "2026-10-18T10:00:00Z", false));
    var model = NewMain();
    var stale = Card(model, "alpha");
    Assert.False(stale.Pinned);

    Seed((Alpha, "2026-10-18T10:00:00Z", true), (Beta, "2026-10-18T12:00:00Z", false));
    model.TogglePinCommand.Execute(stale);

    Assert.False(StoredEntry(Alpha)["pinned"]?.GetValue<bool>() ?? false);
    Assert.Equal(Beta, (string)StoredEntry(Beta)["path"]!);
    Assert.Equal(2, StoredPaths().Length);
    Assert.Equal(["beta", "alpha"], Names(model));
  }

  [Fact]
  public void NothingToChangeLeavesTheFileUntouched()
  {
    Seed((Alpha, "2026-10-18T10:00:00Z", false));
    var model = NewMain();
    var before = _rig.Files.TextOf(_rig.RecentsPath);

    model.RemoveProjectCommand.Execute(new Project { Name = "ghost", Path = "/work/ghost", Edited = "" });
    Assert.Equal(before, _rig.Files.TextOf(_rig.RecentsPath));

    model.RemoveProjectCommand.Execute(Card(model, "alpha"));
    Assert.NotEqual(before, _rig.Files.TextOf(_rig.RecentsPath));
  }

  [Theory]
  [InlineData("pin")]
  [InlineData("remove")]
  public void AChangeDuringAFailedReadLeavesTheFileAloneAndToasts(string change)
  {
    Seed((Alpha, "2026-10-18T10:00:00Z", false), (Beta, "2026-10-18T12:00:00Z", false));
    var model = NewMain();
    var before = _rig.Files.TextOf(_rig.RecentsPath);
    var alpha = Card(model, "alpha");
    _rig.Files.FailReads = true;

    if (change == "pin")
    {
      model.TogglePinCommand.Execute(alpha);
    }
    else
    {
      model.RemoveProjectCommand.Execute(alpha);
    }

    Assert.Equal(before, _rig.Files.TextOf(_rig.RecentsPath));
    Assert.True(model.HasToast);
    Assert.Contains("Could not read", model.ToastMessage);
    Assert.Equal(["beta", "alpha"], Names(model));

    _rig.Files.FailReads = false;
    model.DismissToastCommand.Execute(null);
    model.RemoveProjectCommand.Execute(alpha);
    Assert.Equal([Beta], StoredPaths());
    Assert.False(model.HasToast);
  }

  [Fact]
  public void AFailedReadStillOpensTheProjectWithoutSavingOverTheFile()
  {
    Seed((Alpha, "2026-10-18T10:00:00Z", false), (Beta, "2026-10-18T12:00:00Z", false));
    var model = NewMain();
    var before = _rig.Files.TextOf(_rig.RecentsPath);
    _rig.Files.FailReads = true;

    model.OpenProjectCommand.Execute(Card(model, "alpha"));

    Assert.Equal(before, _rig.Files.TextOf(_rig.RecentsPath));
    Assert.Single(_runner.Launches);
  }

  [Fact]
  public void RefreshDuringAFailedReadKeepsTheListAndRecoversAfterwards()
  {
    Seed((Alpha, "2026-10-18T10:00:00Z", false));
    var model = NewMain();
    _rig.Files.FailReads = true;

    model.Refresh();

    Assert.Equal(["alpha"], Names(model));
    Assert.Equal("Recent Projects", model.ViewTitle);

    _rig.Files.FailReads = false;
    Seed((Alpha, "2026-10-18T10:00:00Z", false), (Beta, "2026-10-18T12:00:00Z", false));
    model.Refresh();
    Assert.Equal(["beta", "alpha"], Names(model));
  }

  [Fact]
  public void CorruptJsonIsStillReplacedByTheNextChange()
  {
    _rig.Files.Seed(_rig.RecentsPath, "this is { not json");
    var model = NewMain();
    Assert.True(model.Projects.IsEmpty);

    model.OpenProjectCommand.Execute(new Project { Name = "alpha", Path = Alpha, Edited = "" });

    Assert.Equal([Alpha], StoredPaths());
    Assert.False(model.Projects.IsEmpty);
  }

  [Fact]
  public void TheWelcomeTitleAndHiddenToggleReturnWhenTheListEmpties()
  {
    Seed((Alpha, "2026-10-18T10:00:00Z", false));
    var model = NewMain();
    Assert.Equal("Recent Projects", model.ViewTitle);
    Assert.True(model.ShowViewToggle);

    model.RemoveProjectCommand.Execute(Card(model, "alpha"));
    Assert.Equal("Welcome to ECS3D", model.ViewTitle);
    Assert.False(model.ShowViewToggle);

    Seed((Alpha, "2026-10-18T10:00:00Z", false));
    model.Refresh();
    Assert.Equal("Recent Projects", model.ViewTitle);
    Assert.True(model.ShowViewToggle);

    _rig.Files.Seed(_rig.RecentsPath, "{\"version\":1,\"projects\":[]}");
    model.Refresh();
    Assert.Equal("Welcome to ECS3D", model.ViewTitle);
    Assert.False(model.ShowViewToggle);
  }
}
