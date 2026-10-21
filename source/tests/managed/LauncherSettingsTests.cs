using System;
using System.IO;
using System.Linq;
using System.Text.Json.Nodes;
using ECS3DLauncher.Services;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherSettingsTests
{
  private const string Documents = "/docs";

  private readonly FakeFileSystem _files = new();
  private readonly RecordingLauncherLog _log = new();
  private readonly UserDataDirectory _userData =
    new(HostOs.Linux, name => name == "XDG_CONFIG_HOME" ? "/cfg" : null);

  private LauncherSettingsStore NewStore() => new(_files, _log, _userData, () => Documents);

  private string SettingsPath => _userData.SettingsFile();

  private JsonObject Saved() => (JsonObject)JsonNode.Parse(_files.TextOf(SettingsPath)!)!;

  [Fact]
  public void SettingsPathIsLauncherSettingsJsonInTheUserDataFolder()
  {
    Assert.Equal(Path.Combine("/cfg", "ECS3D", "launcher-settings.json"), SettingsPath);
    Assert.Equal(SettingsPath, NewStore().FilePath);
    Assert.Equal(Path.Combine("/cfg", "ECS3D", "logs", "launcher.log"), _userData.LauncherLogFile());
  }

  [Fact]
  public void MissingFileGivesDefaultsWithoutAWarning()
  {
    var settings = NewStore().Load();

    Assert.Null(settings.EngineDirectory);
    Assert.Equal(Path.Combine(Documents, "ECS3D Projects"), settings.ProjectsDirectory);
    Assert.False(settings.EditorConsole);
    Assert.False(settings.ServerConsole);
    Assert.False(settings.CloseAfterOpen);
    Assert.Equal(EnginePorts.Default, settings.HostPort);
    Assert.Equal(3000, settings.HostPort);
    Assert.Null(settings.LastJoin);
    Assert.Equal("grid", settings.ProjectsView);
    Assert.Empty(_log.Warnings);
  }

  [Fact]
  public void EveryFieldSurvivesASaveAndLoad()
  {
    var written = new LauncherSettings
    {
      EngineDirectory = "/engine",
      ProjectsDirectory = "/work/projects",
      EditorConsole = true,
      ServerConsole = true,
      CloseAfterOpen = true,
      HostPort = 4242,
      LastJoin = new LastJoin("192.168.1.24", 3001),
      ProjectsView = "list",
    };

    Assert.True(NewStore().Save(written));
    var loaded = NewStore().Load();

    Assert.Equal(written, loaded);
    Assert.Equal(1, (int)Saved()["version"]!);
    Assert.Contains(Path.GetDirectoryName(SettingsPath)!, _files.CreatedDirectories);
    Assert.Empty(_log.Warnings);
  }

  [Fact]
  public void UnknownTopLevelAndNestedFieldsAreKept()
  {
    _files.Seed(SettingsPath, """
      { "version": 1, "futureThing": { "a": [1, 2] }, "hostPort": 3000,
        "lastJoin": { "address": "10.0.0.2", "port": 3000, "nickname": "lan" } }
      """);
    var store = NewStore();
    var loaded = store.Load();

    Assert.True(store.Save(loaded with { HostPort = 3100, LastJoin = new LastJoin("10.0.0.3", 3002) }));

    var saved = Saved();
    Assert.Equal(2, saved["futureThing"]!["a"]!.AsArray().Count);
    Assert.Equal("lan", (string)saved["lastJoin"]!["nickname"]!);
    Assert.Equal("10.0.0.3", (string)saved["lastJoin"]!["address"]!);
    Assert.Equal(3100, (int)saved["hostPort"]!);
  }

  [Fact]
  public void ClearingLastJoinRemovesTheKey()
  {
    var store = NewStore();
    Assert.True(store.Save(store.Defaults() with { LastJoin = new LastJoin("a", 1) }));
    Assert.True(Saved().ContainsKey("lastJoin"));

    Assert.True(store.Save(store.Defaults()));

    Assert.False(Saved().ContainsKey("lastJoin"));
  }

  [Theory]
  [InlineData("{ not json")]
  [InlineData("[1, 2]")]
  [InlineData("null")]
  public void UnreadableFileGivesDefaultsOneWarningAndIsReplacedOnSave(string content)
  {
    _files.Seed(SettingsPath, content);
    var store = NewStore();

    var settings = store.Load();

    Assert.Equal(store.Defaults(), settings);
    var warning = Assert.Single(_log.Warnings);
    Assert.Contains("launcher-settings.json", warning);
    Assert.True(store.Save(settings with { HostPort = 3500 }));
    Assert.Equal(3500, (int)Saved()["hostPort"]!);
  }

  [Fact]
  public void ReadFailureGivesDefaultsAndOneWarning()
  {
    _files.Seed(SettingsPath, "{}");
    _files.FailReads = true;

    var settings = NewStore().Load();

    Assert.Equal(EnginePorts.Default, settings.HostPort);
    Assert.Contains("read failed", Assert.Single(_log.Warnings));
  }

  [Fact]
  public void BadFieldsFallBackIndividuallyAndGoodOnesKeepTheirValues()
  {
    _files.Seed(SettingsPath, """
      { "engineDirectory": 5, "projectsDirectory": "/keep/me", "editorConsole": "yes",
        "serverConsole": true, "closeAfterOpen": 1, "hostPort": 70000,
        "lastJoin": { "address": "h", "port": 0 }, "projectsView": "tiles" }
      """);

    var settings = NewStore().Load();

    Assert.Equal("/keep/me", settings.ProjectsDirectory);
    Assert.True(settings.ServerConsole);
    Assert.Null(settings.EngineDirectory);
    Assert.False(settings.EditorConsole);
    Assert.False(settings.CloseAfterOpen);
    Assert.Equal(3000, settings.HostPort);
    Assert.Null(settings.LastJoin);
    Assert.Equal("grid", settings.ProjectsView);
    foreach (var field in new[] { "engineDirectory", "editorConsole", "closeAfterOpen", "hostPort", "projectsView" })
    {
      Assert.Contains(_log.Warnings, line => line.Contains($"'{field}'"));
    }

    Assert.DoesNotContain(_log.Warnings, line => line.Contains("'projectsDirectory'"));
    Assert.DoesNotContain(_log.Warnings, line => line.Contains("'serverConsole'"));
  }

  [Fact]
  public void ExplicitNullEngineDirectoryIsValidAndSilent()
  {
    _files.Seed(SettingsPath, """{ "engineDirectory": null, "lastJoin": null }""");

    var settings = NewStore().Load();

    Assert.Null(settings.EngineDirectory);
    Assert.Null(settings.LastJoin);
    Assert.Empty(_log.Warnings);
  }

  [Fact]
  public void FailedWriteReturnsFalseLogsAndLeavesTheOldFile()
  {
    _files.Seed(SettingsPath, """{ "hostPort": 3100 }""");
    var store = NewStore();
    var loaded = store.Load();
    var before = _files.TextOf(SettingsPath);

    _files.FailWrites = true;
    Assert.False(store.Save(loaded with { HostPort = 3200 }));
    _files.FailWrites = false;
    _files.FailMoves = true;
    Assert.False(store.Save(loaded with { HostPort = 3200 }));

    Assert.Equal(before, _files.TextOf(SettingsPath));
    Assert.False(_files.HasTemp(SettingsPath));
    Assert.Equal(2, _log.Warnings.Count);
  }

  [Fact]
  public void SuccessfulSaveWritesTheFileAndNoTempFile()
  {
    var store = NewStore();

    Assert.True(store.Save(store.Defaults()));

    Assert.NotNull(_files.TextOf(SettingsPath));
    Assert.False(_files.HasTemp(SettingsPath));
  }

  [Fact]
  public void ToLaunchOptionsMapsEditorThenServerConsole()
  {
    var editorOnly = new LauncherSettings { EditorConsole = true, ServerConsole = false };
    var serverOnly = editorOnly with { EditorConsole = false, ServerConsole = true };

    Assert.Equal(new LaunchOptions(true, false), editorOnly.ToLaunchOptions());
    Assert.Equal(new LaunchOptions(false, true), serverOnly.ToLaunchOptions());
  }

  [Theory]
  [InlineData("0", false, 0)]
  [InlineData("1", true, 1)]
  [InlineData("65535", true, 65535)]
  [InlineData("65536", false, 0)]
  [InlineData(" 3000 ", true, 3000)]
  [InlineData("30a0", false, 0)]
  [InlineData("", false, 0)]
  [InlineData("-5", false, 0)]
  [InlineData("+3000", false, 0)]
  [InlineData("99999999999", false, 0)]
  public void PortParsingAcceptsOnlyWholeNumbersInRange(string text, bool ok, int expected)
  {
    Assert.Equal(ok, SettingsInput.TryParsePort(text, out var port));
    Assert.Equal(expected, port);
  }

  [Fact]
  public void NullPortTextIsRejected()
  {
    Assert.False(SettingsInput.TryParsePort(null, out _));
  }
}

public sealed class LauncherPhysicalFileTests : IDisposable
{
  private readonly string _folder =
    Path.Combine(Path.GetTempPath(), "ecs3d-launcher-" + Guid.NewGuid().ToString("N"));

  public LauncherPhysicalFileTests()
  {
    Directory.CreateDirectory(_folder);
  }

  public void Dispose()
  {
    Directory.Delete(_folder, true);
  }

  [Fact]
  public void AtomicWriteReplacesContentAndLeavesNoTempFile()
  {
    var files = new PhysicalFileSystem();
    var path = Path.Combine(_folder, "settings.json");

    files.WriteAllTextAtomic(path, "first");
    files.WriteAllTextAtomic(path, "second");

    Assert.Equal("second", files.ReadAllText(path));
    var names = Directory.GetFiles(_folder).Select(file => Path.GetFileName(file)).ToArray();
    Assert.Equal(new[] { "settings.json" }, names);
  }

  [Fact]
  public void AtomicWriteThatFailsLeavesNoTempFile()
  {
    var files = new PhysicalFileSystem();
    var target = Path.Combine(_folder, "settings.json");
    Directory.CreateDirectory(target);

    Assert.ThrowsAny<Exception>(() => files.WriteAllTextAtomic(target, "x"));

    Assert.False(File.Exists(target + ".tmp"));
    Assert.True(Directory.Exists(target));
  }

  [Fact]
  public void LauncherLogTruncatesOnFirstWriteThenAppendsWithUtcTimestamps()
  {
    var path = Path.Combine(_folder, "logs", "launcher.log");
    Directory.CreateDirectory(Path.GetDirectoryName(path)!);
    File.WriteAllText(path, "old run\n");
    var now = new DateTimeOffset(2026, 10, 18, 21, 2, 11, TimeSpan.FromHours(2));
    var log = new FileLauncherLog(path, () => now);

    log.Warn("first");
    log.Warn("second");

    var expected = new[] { "2026-10-18T19:02:11.000Z [warn] first", "2026-10-18T19:02:11.000Z [warn] second" };
    Assert.Equal(expected, File.ReadAllLines(path));
  }

  [Fact]
  public void LauncherLogSwallowsFailures()
  {
    var blocker = Path.Combine(_folder, "blocker");
    File.WriteAllText(blocker, "a file, not a folder");
    var log = new FileLauncherLog(Path.Combine(blocker, "launcher.log"), () => DateTimeOffset.UnixEpoch);

    log.Warn("dropped");

    Assert.Equal("a file, not a folder", File.ReadAllText(blocker));
  }
}
