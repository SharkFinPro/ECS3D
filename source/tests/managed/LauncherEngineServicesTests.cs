using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using ECS3DLauncher.Services;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherEngineServicesTests
{
  // Static initializers run in textual order, so EngineRoot comes before the fields built from it.
  private static readonly string EngineRoot = Path.GetFullPath("engine");
  private static readonly string EngineDir = Path.GetFullPath(Path.Combine(EngineRoot, "bin"));
  private static readonly string LauncherDir = Path.GetFullPath(Path.Combine("launcher", "dir"));
  private static readonly LaunchOptions NoOptions = new(false, false);
  private static readonly LaunchOptions BothOptions = new(true, true);

  private static string[] AllFiles(string folder, HostOs os) =>
    Enum.GetValues<EngineExecutable>()
      .Select(executable => Path.Combine(folder, EngineExecutableNames.FileName(executable, os)))
      .ToArray();

  private static EngineLocation CompleteEngine()
  {
    var files = new FakeFileSystem(AllFiles(EngineRoot, HostOs.Linux));
    return new EngineLocator(files, HostOs.Linux, LauncherDir).Locate(EngineRoot);
  }

  [Fact]
  public void WindowsNamesGetExeAndOthersDoNot()
  {
    Assert.Equal("ECS3DEditor.exe", EngineExecutableNames.FileName(EngineExecutable.Editor, HostOs.Windows));
    Assert.Equal("ECS3DClient.exe", EngineExecutableNames.FileName(EngineExecutable.Client, HostOs.Windows));
    Assert.Equal("ECS3DServer.exe", EngineExecutableNames.FileName(EngineExecutable.Server, HostOs.Windows));
    Assert.Equal("ECS3DEditor", EngineExecutableNames.FileName(EngineExecutable.Editor, HostOs.Linux));
    Assert.Equal("ECS3DClient", EngineExecutableNames.FileName(EngineExecutable.Client, HostOs.MacOs));
    Assert.Equal("ECS3DServer", EngineExecutableNames.FileName(EngineExecutable.Server, HostOs.Linux));
  }

  [Fact]
  public void LocatorFindsEverythingWhenAllExecutablesArePresent()
  {
    var files = new FakeFileSystem(AllFiles(LauncherDir, HostOs.Windows));
    var locator = new EngineLocator(files, HostOs.Windows, LauncherDir);

    var location = locator.Locate(null);

    Assert.True(location.IsComplete);
    Assert.Null(location.Describe());
    Assert.Equal(LauncherDir, location.Folder);
    Assert.Equal(Path.Combine(LauncherDir, "ECS3DEditor.exe"), location.Found[EngineExecutable.Editor]);
    Assert.Equal(3, location.Found.Count);
  }

  [Fact]
  public void OverrideFolderWinsOverTheLauncherDirectory()
  {
    var files = AllFiles(EngineDir, HostOs.Linux).Concat(AllFiles(LauncherDir, HostOs.Linux)).ToArray();
    var locator = new EngineLocator(new FakeFileSystem(files), HostOs.Linux, LauncherDir);

    var location = locator.Locate(EngineDir);

    Assert.Equal(EngineDir, location.Folder);
    Assert.Equal(Path.Combine(EngineDir, "ECS3DServer"), location.Found[EngineExecutable.Server]);
  }

  [Theory]
  [InlineData(null)]
  [InlineData("")]
  [InlineData("   ")]
  public void EmptyOverrideFallsBackToTheLauncherDirectory(string? overrideFolder)
  {
    var locator = new EngineLocator(new FakeFileSystem(AllFiles(LauncherDir, HostOs.Linux)), HostOs.Linux, LauncherDir);

    var location = locator.Locate(overrideFolder);

    Assert.Equal(LauncherDir, location.Folder);
    Assert.True(location.IsComplete);
  }

  [Fact]
  public void MissingExecutablesAreListedWithTheirExpectedPaths()
  {
    var present = Path.Combine(EngineDir, "ECS3DEditor.exe");
    var locator = new EngineLocator(new FakeFileSystem(present), HostOs.Windows, LauncherDir);

    var location = locator.Locate(EngineDir);

    Assert.False(location.IsComplete);
    Assert.Equal(present, location.Found[EngineExecutable.Editor]);
    Assert.Equal(
      new[] { EngineExecutable.Client, EngineExecutable.Server },
      location.Missing.Select(item => item.Executable).ToArray());
    Assert.Equal(Path.Combine(EngineDir, "ECS3DClient.exe"), location.Missing[0].ExpectedPath);
    Assert.Equal($"ECS3DClient.exe and ECS3DServer.exe were not found in {EngineDir}", location.Describe());
  }

  [Fact]
  public void RelativeOverrideResolvesToAbsolutePaths()
  {
    var relative = Path.Combine("..", "somewhere", "engine");
    var absolute = Path.GetFullPath(relative);
    var locator = new EngineLocator(new FakeFileSystem(AllFiles(absolute, HostOs.Linux)), HostOs.Linux, LauncherDir);

    var location = locator.Locate(relative);

    Assert.True(location.IsComplete);
    Assert.Equal(absolute, location.Folder);
    Assert.All(location.Found.Values, path => Assert.True(Path.IsPathRooted(path)));
    Assert.Equal(Path.Combine(absolute, "ECS3DClient"), location.Found[EngineExecutable.Client]);
  }

  [Fact]
  public void SingleMissingExecutableReadsAsOneSentence()
  {
    var files = AllFiles(EngineDir, HostOs.Windows).Where(path => !path.EndsWith("ECS3DEditor.exe")).ToArray();
    var locator = new EngineLocator(new FakeFileSystem(files), HostOs.Windows, LauncherDir);

    Assert.Equal($"ECS3DEditor.exe was not found in {EngineDir}", locator.Locate(EngineDir).Describe());
  }

  [Fact]
  public void NothingFoundListsAllThree()
  {
    var location = new EngineLocator(new FakeFileSystem(), HostOs.Linux, LauncherDir).Locate(null);

    Assert.Equal(3, location.Missing.Count);
    Assert.Equal(
      $"ECS3DEditor, ECS3DClient and ECS3DServer were not found in {LauncherDir}",
      location.Describe());
  }

  [Fact]
  public void OpenProjectPassesExactlyTheProjectArguments()
  {
    var runner = new FakeProcessRunner();

    var result = new EngineLauncher(runner, CompleteEngine()).OpenProject("/p/Courtyard", NoOptions);

    Assert.True(result.Succeeded);
    var launch = Assert.Single(runner.Launches);
    Assert.Equal(new[] { "--project", "/p/Courtyard" }, launch.Arguments.ToArray());
    Assert.Equal(Path.Combine(EngineRoot, "ECS3DEditor"), launch.FileName);
    Assert.False(launch.HideWindow);
  }

  [Fact]
  public void OptionsAreAppendedLastInFixedOrder()
  {
    var runner = new FakeProcessRunner();
    var launcher = new EngineLauncher(runner, CompleteEngine());

    launcher.OpenProject("/p", BothOptions);
    launcher.OpenProject("/p", new LaunchOptions(false, true));
    launcher.OpenProject("/p", new LaunchOptions(true, false));

    Assert.Equal(new[] { "--project", "/p", "--console", "--server-console" }, runner.Launches[0].Arguments.ToArray());
    Assert.Equal(new[] { "--project", "/p", "--server-console" }, runner.Launches[1].Arguments.ToArray());
    Assert.Equal(new[] { "--project", "/p", "--console" }, runner.Launches[2].Arguments.ToArray());
  }

  [Fact]
  public void ExploreHasNoProjectButKeepsOptions()
  {
    var runner = new FakeProcessRunner();
    var launcher = new EngineLauncher(runner, CompleteEngine());

    launcher.ExploreEngine(NoOptions);
    launcher.ExploreEngine(BothOptions);

    Assert.Empty(runner.Launches[0].Arguments);
    Assert.Equal(new[] { "--console", "--server-console" }, runner.Launches[1].Arguments.ToArray());
    Assert.DoesNotContain("--project", runner.Launches[1].Arguments);
  }

  [Fact]
  public void PlayUsesTheClientWithoutOptions()
  {
    var runner = new FakeProcessRunner();

    new EngineLauncher(runner, CompleteEngine()).PlayProject("/p/Sample");

    var launch = Assert.Single(runner.Launches);
    Assert.Equal(Path.Combine(EngineRoot, "ECS3DClient"), launch.FileName);
    Assert.Equal(new[] { "--project", "/p/Sample" }, launch.Arguments.ToArray());
  }

  [Fact]
  public void ServerStartsHiddenWithThePortAsAString()
  {
    var runner = new FakeProcessRunner();
    var launcher = new EngineLauncher(runner, CompleteEngine());

    launcher.StartServer("/p/Coin", 3000);
    launcher.PlayProject("/p/Coin");

    Assert.Equal(new[] { "--project", "/p/Coin", "--port", "3000" }, runner.Launches[0].Arguments.ToArray());
    Assert.Equal(Path.Combine(EngineRoot, "ECS3DServer"), runner.Launches[0].FileName);
    Assert.True(runner.Launches[0].HideWindow);
    Assert.False(runner.Launches[1].HideWindow);
  }

  [Fact]
  public void EveryLaunchRunsInTheEngineFolder()
  {
    var runner = new FakeProcessRunner();
    var launcher = new EngineLauncher(runner, CompleteEngine());

    launcher.OpenProject("/p", NoOptions);
    launcher.ExploreEngine(NoOptions);
    launcher.PlayProject("/p");
    launcher.StartServer("/p", 3000);

    Assert.Equal(4, runner.Launches.Count);
    Assert.All(runner.Launches, launch => Assert.Equal(EngineRoot, launch.WorkingDirectory));
  }

  [Fact]
  public void ArgumentsWithSpacesStayOneElement()
  {
    var runner = new FakeProcessRunner();

    new EngineLauncher(runner, CompleteEngine()).OpenProject("/my projects/Cool Game", NoOptions);

    Assert.Equal("/my projects/Cool Game", runner.Launches[0].Arguments[1]);
  }

  [Fact]
  public void MissingExecutableNeverReachesTheRunner()
  {
    var files = AllFiles(EngineRoot, HostOs.Linux).Where(path => !path.EndsWith("ECS3DEditor")).ToArray();
    var location = new EngineLocator(new FakeFileSystem(files), HostOs.Linux, LauncherDir).Locate(EngineRoot);
    var runner = new FakeProcessRunner();
    var launcher = new EngineLauncher(runner, location);

    var missing = launcher.OpenProject("/p", NoOptions);
    var present = launcher.PlayProject("/p");

    Assert.False(missing.Succeeded);
    Assert.Equal($"ECS3DEditor was not found in {EngineRoot}", missing.Error);
    Assert.True(present.Succeeded);
    Assert.Single(runner.Launches);
  }

  [Fact]
  public void StartFailureSurfacesAsAnErrorResult()
  {
    var runner = new FakeProcessRunner { FailWith = "ECS3DEditor could not be started: access denied" };
    var launcher = new EngineLauncher(runner, CompleteEngine());

    var failed = launcher.OpenProject("/p", NoOptions);
    runner.FailWith = null;
    var started = launcher.OpenProject("/p", NoOptions);

    Assert.False(failed.Succeeded);
    Assert.Null(failed.Process);
    Assert.Equal("ECS3DEditor could not be started: access denied", failed.Error);
    Assert.True(started.Succeeded);
    Assert.Null(started.Error);
  }

  private static readonly TimeSpan ShortWindow = TimeSpan.FromMilliseconds(50);

  private static EarlyExitWatcher Watcher(TimeSpan window) =>
    new(new UserDataDirectory(HostOs.Linux, name => name == "HOME" ? "/home/u" : null), window);

  [Theory]
  [InlineData(EngineExecutable.Editor, "editor.log")]
  [InlineData(EngineExecutable.Client, "client.log")]
  [InlineData(EngineExecutable.Server, "server.log")]
  public async Task NonZeroExitBeforeTheWindowEndsIsReported(EngineExecutable kind, string logName)
  {
    var process = new FakeRunningProcess();

    var watch = Watcher(TimeSpan.FromMinutes(1)).WatchAsync(process, kind, CancellationToken.None);
    process.Exit(3);
    var result = await watch;

    Assert.Equal(EarlyExitKind.ExitedEarly, result.Kind);
    Assert.Equal(kind, result.Executable);
    Assert.Equal(3, result.ExitCode);
    Assert.Equal(Path.Combine("/home/u", ".config", "ECS3D", "logs", logName), result.LogPath);
  }

  [Fact]
  public async Task ZeroExitIsCleanAndHasNoLog()
  {
    var process = new FakeRunningProcess();

    var watch = Watcher(TimeSpan.FromMinutes(1)).WatchAsync(process, EngineExecutable.Editor, CancellationToken.None);
    process.Exit(0);
    var result = await watch;

    Assert.Equal(EarlyExitKind.ExitedCleanly, result.Kind);
    Assert.Null(result.LogPath);
  }

  [Fact]
  public async Task ProcessStillRunningWhenTheWindowEndsIsNotReported()
  {
    var process = new FakeRunningProcess();

    var result = await Watcher(ShortWindow).WatchAsync(process, EngineExecutable.Server, CancellationToken.None);

    Assert.Equal(EarlyExitKind.StillRunning, result.Kind);
    Assert.Null(result.ExitCode);
    Assert.Null(result.LogPath);
  }

  [Fact]
  public async Task ACancelledWatchIsDistinctFromAnExpiredWindow()
  {
    using var cancel = new CancellationTokenSource();
    var process = new FakeRunningProcess();

    var watch = Watcher(TimeSpan.FromMinutes(1)).WatchAsync(process, EngineExecutable.Client, cancel.Token);
    cancel.Cancel();
    var result = await watch;

    Assert.Equal(EarlyExitKind.Cancelled, result.Kind);
    Assert.Null(result.ExitCode);
  }

  private static UserDataDirectory UserData(HostOs os, params (string Name, string? Value)[] variables)
  {
    var map = variables.ToDictionary(pair => pair.Name, pair => pair.Value);
    return new UserDataDirectory(os, name => map.GetValueOrDefault(name));
  }

  [Fact]
  public void WindowsUsesAppData()
  {
    var directory = UserData(HostOs.Windows, ("APPDATA", "C:/Roaming"), ("HOME", "/ignored"));

    Assert.Equal(Path.Combine("C:/Roaming", "ECS3D"), directory.Resolve());
  }

  [Fact]
  public void MacUsesApplicationSupportUnderHome()
  {
    var directory = UserData(HostOs.MacOs, ("HOME", "/Users/u"), ("XDG_CONFIG_HOME", "/ignored"));

    Assert.Equal(Path.Combine("/Users/u", "Library", "Application Support", "ECS3D"), directory.Resolve());
  }

  [Fact]
  public void LinuxPrefersXdgConfigHome()
  {
    var directory = UserData(HostOs.Linux, ("XDG_CONFIG_HOME", "/xdg"), ("HOME", "/home/u"));

    Assert.Equal(Path.Combine("/xdg", "ECS3D"), directory.Resolve());
  }

  [Theory]
  [InlineData(null)]
  [InlineData("")]
  public void LinuxFallsBackToHomeConfigWhenXdgIsUnsetOrEmpty(string? xdg)
  {
    var directory = UserData(HostOs.Linux, ("XDG_CONFIG_HOME", xdg), ("HOME", "/home/u"));

    Assert.Equal(Path.Combine("/home/u", ".config", "ECS3D"), directory.Resolve());
  }

  [Theory]
  [InlineData(HostOs.Windows)]
  [InlineData(HostOs.MacOs)]
  [InlineData(HostOs.Linux)]
  public void NothingResolvableGivesTheRelativeFolder(HostOs os)
  {
    var empty = UserData(os, ("APPDATA", ""), ("HOME", ""), ("XDG_CONFIG_HOME", ""));
    var unset = UserData(os);

    Assert.Equal("ECS3D", empty.Resolve());
    Assert.Equal("ECS3D", unset.Resolve());
  }

  [Fact]
  public void LogFileLivesInTheLogsFolder()
  {
    var directory = UserData(HostOs.Windows, ("APPDATA", "C:/Roaming"));

    Assert.Equal(Path.Combine("C:/Roaming", "ECS3D", "logs", "server.log"), directory.LogFile(EngineExecutable.Server));
  }

  [Fact]
  public void VersionDropsTheCommitHashSuffix()
  {
    Assert.Equal("1.2.3", AppVersion.Choose("1.2.3+abc123", "9.9.9.9"));
    Assert.Equal("1.2.3-beta.1", AppVersion.Choose("1.2.3-beta.1+abc123", null));
    Assert.Equal("1.2.3", AppVersion.Choose("1.2.3", "9.9.9.9"));
  }

  [Theory]
  [InlineData(null)]
  [InlineData("")]
  [InlineData("+abc123")]
  public void VersionFallsBackToTheAssemblyVersion(string? informational)
  {
    Assert.Equal("0.4.0.0", AppVersion.Choose(informational, "0.4.0.0"));
  }

  [Fact]
  public void VersionFallsBackToDevWithNothingElse()
  {
    Assert.Equal("dev", AppVersion.Choose(null, null));
    Assert.Equal("dev", AppVersion.Choose("", "  "));
    Assert.Equal("dev", AppVersion.From(null));
  }

  [Fact]
  public void VersionIsReadFromTheGivenAssembly()
  {
    var assembly = typeof(LauncherEngineServicesTests).Assembly;
    var informational = assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion;

    Assert.False(string.IsNullOrEmpty(informational));
    Assert.Equal(informational!.Split('+')[0], AppVersion.From(assembly));
  }

  private static string SourceRoot() =>
    typeof(LauncherEngineServicesTests).Assembly
      .GetCustomAttributes<AssemblyMetadataAttribute>()
      .Single(a => a.Key == "ECS3DSourceRoot").Value!;

  private static string EngineVersionFile() =>
    File.ReadAllText(Path.Combine(SourceRoot(), "..", "VERSION")).Trim();

  [Fact]
  public void VersionIsTheEngineVersionFile()
  {
    Assert.Equal(EngineVersionFile(), AppVersion.From(typeof(LauncherEngineServicesTests).Assembly));
  }

  [Fact]
  public void EngineVersionFileIsAVersion()
  {
    Assert.True(EngineVersion.TryParse(EngineVersionFile(), out _));
  }

  [Fact]
  public void NoShippedTemplateNeedsANewerEngine()
  {
    var engine = EngineVersionFile();
    var manifests = Directory.GetFiles(
      Path.Combine(SourceRoot(), "templates"),
      "template.json",
      SearchOption.AllDirectories);

    Assert.NotEmpty(manifests);
    foreach (var manifest in manifests)
    {
      using var document = System.Text.Json.JsonDocument.Parse(File.ReadAllText(manifest));
      var minimum = document.RootElement.GetProperty("minEngineVersion").GetString()!;
      Assert.True(EngineVersion.TryParse(minimum, out _), manifest);
      Assert.False(TemplateCompatibility.NeedsNewerEngine(minimum, engine), manifest);
    }
  }

  [Fact]
  public void AMinimumOneMajorAboveTheEngineIsGated()
  {
    var engine = EngineVersionFile();
    var major = int.Parse(engine.Split('.')[0]);

    Assert.True(TemplateCompatibility.NeedsNewerEngine($"{major + 1}.0.0", engine));
  }
}
