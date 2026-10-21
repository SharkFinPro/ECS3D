using System;
using System.Threading.Tasks;
using ECS3DLauncher.Services;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherShellTests
{
  private static readonly TimeSpan LongWindow = TimeSpan.FromHours(1);
  private static readonly TimeSpan ShortWindow = TimeSpan.FromMilliseconds(20);

  [Theory]
  [InlineData(LauncherTab.Projects, "Recent Projects", "Search projects", true, true)]
  [InlineData(LauncherTab.Templates, "Templates", "Search templates", true, false)]
  [InlineData(LauncherTab.Samples, "Samples", "Search samples", true, false)]
  [InlineData(LauncherTab.Learn, "Learn", "Search guides", true, false)]
  [InlineData(LauncherTab.Settings, "Settings", "", false, false)]
  public void HeaderTableMatchesTheSpec(
    LauncherTab tab, string title, string placeholder, bool showSearch, bool showToggle)
  {
    var header = LauncherTabs.Header(tab);

    Assert.Equal(title, header.Title);
    Assert.Equal(placeholder, header.SearchPlaceholder);
    Assert.Equal(showSearch, header.ShowSearch);
    Assert.Equal(showToggle, header.ShowViewToggle);
    Assert.False(string.IsNullOrWhiteSpace(header.Subtitle));
  }

  [Fact]
  public void SettingsSubtitlePointsAtTheEditorsOwnSettings()
  {
    Assert.Equal(
      "Launcher preferences. Editor preferences live in the editor's own Settings.",
      LauncherTabs.Header(LauncherTab.Settings).Subtitle);
  }

  [Fact]
  public void TabKeysRoundTripAndUnknownKeysOpenProjects()
  {
    foreach (var tab in Enum.GetValues<LauncherTab>())
    {
      Assert.Equal(tab, LauncherTabs.Parse(LauncherTabs.Key(tab)));
    }

    Assert.Equal("settings", LauncherTabs.Key(LauncherTab.Settings));
    Assert.Equal(LauncherTab.Projects, LauncherTabs.Parse("nonsense"));
    Assert.Equal(LauncherTab.Projects, LauncherTabs.Parse(null));
  }

  [Fact]
  public void CompleteEngineEnablesEveryAction()
  {
    var actions = new EngineActions(ShellFixtures.EngineWithout());

    foreach (var state in new[] { actions.Open, actions.Explore, actions.Play, actions.Host })
    {
      Assert.True(state.Available);
      Assert.Null(state.Tooltip);
    }
  }

  [Fact]
  public void MissingEditorDisablesOpenAndExploreOnly()
  {
    var actions = new EngineActions(ShellFixtures.EngineWithout(EngineExecutable.Editor));
    var expected = $"ECS3DEditor was not found in {ShellFixtures.EngineFolder}";

    Assert.False(actions.Open.Available);
    Assert.Equal(expected, actions.Open.Tooltip);
    Assert.False(actions.Explore.Available);
    Assert.Equal(expected, actions.Explore.Tooltip);
    Assert.True(actions.Play.Available);
    Assert.True(actions.Host.Available);
  }

  [Fact]
  public void MissingClientDisablesPlayAndHost()
  {
    var actions = new EngineActions(ShellFixtures.EngineWithout(EngineExecutable.Client));
    var expected = $"ECS3DClient was not found in {ShellFixtures.EngineFolder}";

    Assert.False(actions.Play.Available);
    Assert.Equal(expected, actions.Play.Tooltip);
    Assert.False(actions.Host.Available);
    Assert.Equal(expected, actions.Host.Tooltip);
    Assert.True(actions.Open.Available);
    Assert.True(actions.Explore.Available);
  }

  [Fact]
  public void MissingServerDisablesHostOnly()
  {
    var actions = new EngineActions(ShellFixtures.EngineWithout(EngineExecutable.Server));

    Assert.False(actions.Host.Available);
    Assert.Equal($"ECS3DServer was not found in {ShellFixtures.EngineFolder}", actions.Host.Tooltip);
    Assert.True(actions.Open.Available);
    Assert.True(actions.Explore.Available);
    Assert.True(actions.Play.Available);
  }

  [Fact]
  public void HostNamesEverythingItNeedsThatIsMissing()
  {
    var engine = ShellFixtures.EngineWithout(EngineExecutable.Client, EngineExecutable.Server);

    var actions = new EngineActions(engine);

    Assert.Equal(
      $"ECS3DClient and ECS3DServer were not found in {ShellFixtures.EngineFolder}",
      actions.Host.Tooltip);
    Assert.Equal(engine.Describe(), actions.Host.Tooltip);
  }

  [Fact]
  public async Task ExploreStartsTheEditorWithNoArguments()
  {
    var runner = new FakeProcessRunner();
    var recorder = new NoticeRecorder();
    var engine = ShellFixtures.EngineWithout();
    var coordinator = ShellFixtures.CoordinatorFor(engine, runner, LongWindow, recorder);

    var watch = coordinator.ExploreEngineAsync(ShellFixtures.NoOptions);
    runner.Last!.Exit(0);
    await watch;

    var launch = Assert.Single(runner.Launches);
    Assert.Equal(engine.Found[EngineExecutable.Editor], launch.FileName);
    Assert.Empty(launch.Arguments);
    Assert.Equal(ShellFixtures.EngineFolder, launch.WorkingDirectory);
  }

  [Fact]
  public async Task OpenPassesTheProjectFolderAsExactlyTwoArguments()
  {
    var runner = new FakeProcessRunner();
    var recorder = new NoticeRecorder();
    var coordinator = ShellFixtures.CoordinatorFor(ShellFixtures.EngineWithout(), runner, LongWindow, recorder);

    var watch = coordinator.OpenProjectAsync("/projects/my game", ShellFixtures.NoOptions);
    runner.Last!.Exit(0);
    await watch;

    var launch = Assert.Single(runner.Launches);
    Assert.Equal(new[] { "--project", "/projects/my game" }, launch.Arguments);
  }

  [Fact]
  public async Task NonZeroExitInsideTheWindowNamesTheExitCodeAndLog()
  {
    var runner = new FakeProcessRunner();
    var recorder = new NoticeRecorder();
    var coordinator = ShellFixtures.CoordinatorFor(ShellFixtures.EngineWithout(), runner, LongWindow, recorder);

    var watch = coordinator.OpenProjectAsync("/p", ShellFixtures.NoOptions);
    runner.Last!.Exit(3);
    await watch;

    var notice = Assert.Single(recorder.Notices);
    Assert.Equal(ShellFixtures.ExpectedLog("editor"), notice.LogPath);
    Assert.Contains("ECS3DEditor", notice.Message);
    Assert.Contains("code 3", notice.Message);
    Assert.Contains(ShellFixtures.ExpectedLog("editor"), notice.Message);
  }

  [Fact]
  public async Task CleanExitProducesNoNotice()
  {
    var runner = new FakeProcessRunner();
    var recorder = new NoticeRecorder();
    var coordinator = ShellFixtures.CoordinatorFor(ShellFixtures.EngineWithout(), runner, LongWindow, recorder);

    var watch = coordinator.ExploreEngineAsync(ShellFixtures.NoOptions);
    runner.Last!.Exit(0);
    await watch;

    Assert.Empty(recorder.Notices);
  }

  [Fact]
  public async Task StillRunningAfterTheWindowProducesNoNoticeAndIsNotKilled()
  {
    var runner = new FakeProcessRunner();
    var recorder = new NoticeRecorder();
    var coordinator = ShellFixtures.CoordinatorFor(ShellFixtures.EngineWithout(), runner, ShortWindow, recorder);

    await coordinator.ExploreEngineAsync(ShellFixtures.NoOptions);

    Assert.Empty(recorder.Notices);
    Assert.False(runner.Last!.HasExited);
    Assert.False(runner.Last.Killed);
  }

  [Fact]
  public async Task StartFailureProducesANoticeWithoutALog()
  {
    var runner = new FakeProcessRunner { FailWith = "access denied" };
    var recorder = new NoticeRecorder();
    var coordinator = ShellFixtures.CoordinatorFor(ShellFixtures.EngineWithout(), runner, LongWindow, recorder);

    await coordinator.OpenProjectAsync("/p", ShellFixtures.NoOptions);

    var notice = Assert.Single(recorder.Notices);
    Assert.Equal("access denied", notice.Message);
    Assert.Null(notice.LogPath);
  }

  [Fact]
  public async Task UnwrappedStartExceptionBecomesANotice()
  {
    var runner = new ThrowingProcessRunner(new InvalidOperationException("bad start"), null);
    var recorder = new NoticeRecorder();
    var coordinator = ShellFixtures.CoordinatorFor(ShellFixtures.EngineWithout(), runner, LongWindow, recorder);

    await coordinator.OpenProjectAsync("/p", ShellFixtures.NoOptions);

    var notice = Assert.Single(recorder.Notices);
    Assert.Equal("bad start", notice.Message);
    Assert.Null(notice.LogPath);
  }

  [Fact]
  public async Task WatchExceptionBecomesANotice()
  {
    var runner = new ThrowingProcessRunner(null, new InvalidOperationException("wait broke"));
    var recorder = new NoticeRecorder();
    var coordinator = ShellFixtures.CoordinatorFor(ShellFixtures.EngineWithout(), runner, LongWindow, recorder);

    await coordinator.ExploreEngineAsync(ShellFixtures.NoOptions);

    Assert.Equal("wait broke", Assert.Single(recorder.Notices).Message);
  }

  [Fact]
  public async Task LaunchingWithoutTheEditorReportsTheMissingFile()
  {
    var runner = new FakeProcessRunner();
    var recorder = new NoticeRecorder();
    var engine = ShellFixtures.EngineWithout(EngineExecutable.Editor);
    var coordinator = ShellFixtures.CoordinatorFor(engine, runner, LongWindow, recorder);

    await coordinator.ExploreEngineAsync(ShellFixtures.NoOptions);

    Assert.Empty(runner.Launches);
    var notice = Assert.Single(recorder.Notices);
    Assert.Equal($"ECS3DEditor was not found in {ShellFixtures.EngineFolder}", notice.Message);
  }

  [Theory]
  [InlineData(EarlyExitKind.ExitedCleanly)]
  [InlineData(EarlyExitKind.StillRunning)]
  [InlineData(EarlyExitKind.Cancelled)]
  public void OnlyAnEarlyExitMakesANotice(EarlyExitKind kind)
  {
    Assert.Null(EarlyExitNotice.From(new EarlyExitResult(kind, EngineExecutable.Client, 0, null)));

    var early = new EarlyExitResult(EarlyExitKind.ExitedEarly, EngineExecutable.Client, 9, "/logs/client.log");
    Assert.Equal("/logs/client.log", EarlyExitNotice.From(early)!.LogPath);
  }

  [Theory]
  [InlineData("0.4.2", "v0.4.2")]
  [InlineData("1.0.0-beta.1", "v1.0.0-beta.1")]
  [InlineData("v2.0", "v2.0")]
  [InlineData("dev", "dev")]
  public void VersionLabelPrefixesAVersionButNotTheFallback(string version, string expected)
  {
    Assert.Equal(expected, VersionLabel.Format(version));
  }

  [Theory]
  [InlineData("", true)]
  [InlineData("  ", true)]
  [InlineData("MARBLE", true)]
  [InlineData("ecs3d", true)]
  [InlineData("voxel", false)]
  public void SearchMatchesNameOrPathIgnoringCase(string query, bool expected)
  {
    Assert.Equal(expected, SearchMatch.Matches(query, "Marble Run", "C:/Projects/ECS3D/marble"));
  }
}
