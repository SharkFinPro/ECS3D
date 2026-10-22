using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using ECS3DLauncher.Services;
using ECS3DLauncher.Shared.Models;
using ECS3DLauncher.Shell;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherMainWindowViewModelTests
{
  private static readonly Project RealProject = new() { Name = "Alpha", Path = "/work/alpha", Edited = "today" };

  private readonly LauncherRig _rig = new();
  private readonly FakeProcessRunner _runner = new();

  private MainWindowViewModel NewMain(Action<Action>? post = null) =>
    new(
      _rig.NewEditor(),
      _runner,
      new EarlyExitWatcher(ShellFixtures.UserData, TimeSpan.FromHours(1)),
      "1.2.3",
      post ?? (action => action()));

  private static object TabModel(MainWindowViewModel model, LauncherTab tab) => tab switch
  {
    LauncherTab.Templates => model.Templates,
    LauncherTab.Samples => model.Samples,
    LauncherTab.Learn => model.Learn,
    LauncherTab.Settings => model.Settings,
    _ => model.Projects,
  };

  private static string[] ActiveKeys(MainWindowViewModel model) =>
    model.NavItems.Where(item => item.IsActive).Select(item => item.Key).ToArray();

  [Fact]
  public void StartsOnTheProjectsTab()
  {
    var model = NewMain();
    var header = LauncherTabs.Header(LauncherTab.Projects);

    Assert.Same(model.Projects, model.CurrentTab);
    Assert.Equal("projects", model.ActiveNav);
    Assert.True(model.Projects.IsGridView);
    Assert.Equal(new[] { "projects" }, ActiveKeys(model));
    Assert.Equal(header.Title, model.ViewTitle);
    Assert.Equal(header.Subtitle, model.ViewSubtitle);
    Assert.Equal(header.SearchPlaceholder, model.SearchPlaceholder);
    Assert.Equal(header.ShowViewToggle, model.ShowViewToggle);
    Assert.Equal(VersionLabel.Format("1.2.3"), model.Version);
  }

  [Theory]
  [InlineData("templates", LauncherTab.Templates)]
  [InlineData("samples", LauncherTab.Samples)]
  [InlineData("learn", LauncherTab.Learn)]
  [InlineData("settings", LauncherTab.Settings)]
  public void SelectingATabShowsItAndUpdatesTheHeaderAndNav(string key, LauncherTab tab)
  {
    var model = NewMain();
    model.SearchText = "query";
    var log = new PropertyLog(model);
    var header = LauncherTabs.Header(tab);

    model.SelectNavCommand.Execute(key);

    Assert.Same(TabModel(model, tab), model.CurrentTab);
    Assert.Equal(header.Title, model.ViewTitle);
    Assert.Equal(header.Subtitle, model.ViewSubtitle);
    Assert.Equal(header.SearchPlaceholder, model.SearchPlaceholder);
    Assert.Equal(header.ShowSearch, model.ShowSearch);
    Assert.Equal(header.ShowViewToggle, model.ShowViewToggle);
    foreach (var name in new[] { "ViewTitle", "ViewSubtitle", "SearchPlaceholder", "ShowSearch", "ShowViewToggle" })
    {
      Assert.Contains(name, log.Names);
    }

    var expectedActive = tab == LauncherTab.Settings ? Array.Empty<string>() : new[] { key };
    Assert.Equal(expectedActive, ActiveKeys(model));
    Assert.Equal("", model.SearchText);
    Assert.Equal("", model.Projects.SearchText);
  }

  [Fact]
  public void AnUnknownKeyFallsBackToProjectsAndTheCommandsOpenTheirTabs()
  {
    var model = NewMain();

    model.SelectNavCommand.Execute("templates");
    model.SelectNavCommand.Execute("bogus");
    Assert.Same(model.Projects, model.CurrentTab);
    Assert.Equal(LauncherTabs.Header(LauncherTab.Projects).Title, model.ViewTitle);

    model.NewProjectCommand.Execute(null);
    Assert.Same(model.Templates, model.CurrentTab);

    model.OpenSettingsCommand.Execute(null);
    Assert.Same(model.Settings, model.CurrentTab);
  }

  [Fact]
  public void SearchTextForwardsToTheProjectsTab()
  {
    var model = NewMain();
    model.Projects.Projects.Add(RealProject);

    model.SearchText = "alp";
    Assert.Equal("alp", model.Projects.SearchText);
    Assert.Equal(2, model.Projects.ProjectTiles.Count);

    model.SearchText = "zzz";
    Assert.Single(model.Projects.ProjectTiles);
  }

  [Fact]
  public void TheProjectsNavCountFollowsTheList()
  {
    var model = NewMain();
    Assert.Equal("0", model.NavItems[0].Count);

    model.Projects.Projects.Add(RealProject);

    Assert.Equal("1", model.NavItems[0].Count);
  }

  [Fact]
  public void TheProjectsViewStartsFromSettingsAndSavesTheChoice()
  {
    const string seed = """{ "projectsView": "list" }""";
    _rig.Files.Seed(_rig.SettingsPath, seed);

    var model = NewMain();

    Assert.False(model.Projects.IsGridView);
    Assert.Equal(seed, _rig.Files.TextOf(_rig.SettingsPath));

    model.Projects.IsGridView = true;

    Assert.Equal("grid", (string)_rig.Saved()!["projectsView"]!);
  }

  [Fact]
  public void FixingTheEngineFolderClearsTheBannerAndEnablesExplore()
  {
    _rig.AddEngine(LauncherRig.OtherDir);
    var model = NewMain();
    var changes = 0;
    model.ExploreCommand.CanExecuteChanged += (_, _) => changes++;
    var log = new PropertyLog(model);
    Assert.True(model.HasEngineBanner);
    Assert.False(model.ExploreCommand.CanExecute(null));
    Assert.NotNull(model.Projects.ExploreTooltip);

    model.Settings.EngineFolder = LauncherRig.OtherDir;

    Assert.False(model.HasEngineBanner);
    Assert.Null(model.EngineBannerText);
    Assert.True(model.ExploreCommand.CanExecute(null));
    Assert.True(changes > 0);
    Assert.Null(model.Projects.ExploreTooltip);
    Assert.Contains(nameof(MainWindowViewModel.EngineBannerText), log.Names);
    Assert.Contains(nameof(MainWindowViewModel.HasEngineBanner), log.Names);
  }

  [Fact]
  public void AMissingClientKeepsTheBannerWhileExploreAndOpenStayEnabled()
  {
    _rig.AddEngine(LauncherRig.LauncherDir, EngineExecutable.Client);

    var model = NewMain();

    Assert.True(model.HasEngineBanner);
    Assert.True(model.ExploreCommand.CanExecute(null));
    Assert.True(model.OpenProjectCommand.CanExecute(RealProject));
  }

  [Fact]
  public void OpeningAProjectLaunchesTheEditorOnlyForARealProjectWithAnEditor()
  {
    var model = NewMain();

    model.OpenProjectCommand.Execute(RealProject);
    Assert.Empty(_runner.Launches);

    _rig.AddEngine(LauncherRig.OtherDir);
    model.Settings.EngineFolder = LauncherRig.OtherDir;

    model.OpenProjectCommand.Execute(model.Projects.ProjectTiles[0]);
    Assert.Empty(_runner.Launches);

    model.OpenProjectCommand.Execute(RealProject);
    var launch = Assert.Single(_runner.Launches);
    Assert.Equal(Path.Combine(LauncherRig.OtherDir, "ECS3DEditor"), launch.FileName);
    Assert.Equal(new[] { "--project", "/work/alpha" }, launch.Arguments.ToArray());
  }

  [Fact]
  public void ExploreLaunchesTheEditorWithoutAProject()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    var model = NewMain();

    model.ExploreCommand.Execute(null);

    Assert.Empty(Assert.Single(_runner.Launches).Arguments);
  }

  [Fact]
  public void AFailedSettingsSaveShowsAToastThatCanBeDismissed()
  {
    var model = NewMain();
    Assert.False(model.HasToast);
    _rig.Files.FailWrites = true;

    model.Settings.EditorConsole = true;

    Assert.True(model.HasToast);
    Assert.Contains(_rig.SettingsPath, model.ToastMessage);
    Assert.False(model.HasToastLog);
    Assert.False(model.OpenLogsCommand.CanExecute(null));

    model.DismissToastCommand.Execute(null);

    Assert.False(model.HasToast);
    Assert.False(model.HasToastLog);
  }

  private async Task<MainWindowViewModel> ModelWithEarlyExitToastAsync()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    var posted = new TaskCompletionSource();
    var model = NewMain(action =>
    {
      action();
      posted.TrySetResult();
    });

    model.OpenProjectCommand.Execute(RealProject);
    _runner.Last!.Exit(1);
    await posted.Task.WaitAsync(TimeSpan.FromSeconds(10));
    return model;
  }

  [Fact]
  public async Task AnEarlyExitToastCarriesTheLogPath()
  {
    var model = await ModelWithEarlyExitToastAsync();

    Assert.True(model.HasToast);
    Assert.Equal(ShellFixtures.ExpectedLog("editor"), model.ToastLogPath);
    Assert.True(model.OpenLogsCommand.CanExecute(null));

    model.DismissToastCommand.Execute(null);
    Assert.False(model.OpenLogsCommand.CanExecute(null));
  }

  [Theory]
  [InlineData("opens")]
  [InlineData("declined")]
  [InlineData("throws")]
  public async Task OpeningTheLogReportsOnlyAFailure(string mode)
  {
    var model = await ModelWithEarlyExitToastAsync();
    var message = model.ToastMessage;
    var path = model.ToastLogPath!;
    string? opened = null;
    model.OpenLogFile = requested =>
    {
      opened = requested;
      return mode == "throws" ? throw new IOException("no handler") : Task.FromResult(mode == "opens");
    };

    model.OpenLogsCommand.Execute(null);

    Assert.Equal(path, opened);
    if (mode == "opens")
    {
      Assert.Equal(message, model.ToastMessage);
      Assert.Equal(path, model.ToastLogPath);
    }
    else
    {
      Assert.Contains(path, model.ToastMessage);
      Assert.NotEqual(message, model.ToastMessage);
      Assert.False(model.HasToastLog);
    }
  }
}
