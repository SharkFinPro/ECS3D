using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json.Nodes;
using ECS3DLauncher.Services;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherRecentProjectsTests
{
  private readonly FakeTreeFileSystem _files = new();
  private readonly RecordingLauncherLog _log = new();
  private readonly FixedTimeProvider _time = new();
  private readonly UserDataDirectory _userData =
    new(HostOs.Linux, name => name == "XDG_CONFIG_HOME" ? "/cfg" : null);

  private string FilePath => _userData.RecentProjectsFile();

  private RecentProjectsStore NewStore() => NewStore(HostOs.Linux);

  private RecentProjectsStore NewStore(HostOs os)
  {
    var store = new RecentProjectsStore(_files, _log, _userData, _time, os, _ => { });
    store.Load();
    return store;
  }

  private JsonObject Saved() => (JsonObject)JsonNode.Parse(_files.TextOf(FilePath)!)!;

  private static List<string> Paths(RecentProjectsStore store) => store.Entries().Select(e => e.Path).ToList();

  [Fact]
  public void FileIsRecentProjectsJsonInTheUserDataFolder()
  {
    Assert.Equal(System.IO.Path.Combine("/cfg", "ECS3D", "recent-projects.json"), FilePath);
    Assert.Equal(FilePath, NewStore().FilePath);
  }

  [Fact]
  public void MissingFileGivesAnEmptyListWithoutAWarning()
  {
    Assert.Empty(NewStore().Entries());
    Assert.Empty(_log.Warnings);
  }

  [Fact]
  public void EntriesSurviveASaveAndLoad()
  {
    var store = NewStore();
    store.RecordOpened("/home/a/Courtyard", "first-person");
    store.SetPinned("/home/a/Courtyard", true);
    store.RecordOpened("/home/a/Game.json");
    Assert.True(store.Save());

    var loaded = NewStore().Entries();

    Assert.Equal(2, loaded.Count);
    Assert.Equal(new RecentProject("/home/a/Game.json", "Game", "2026-10-18T19:02:11Z", false, null), loaded[0]);
    Assert.Equal(new RecentProject("/home/a/Courtyard", "Courtyard", "2026-10-18T19:02:11Z", true, "first-person"), loaded[1]);
    Assert.Equal(1, (int)Saved()["version"]!);
    Assert.Empty(_log.Warnings);
  }

  [Fact]
  public void SavingCreatesTheDirectoryAndFailureWarnsInsteadOfThrowing()
  {
    var store = NewStore();
    store.RecordOpened("/home/a/Proj");

    Assert.True(store.Save());
    Assert.Equal([System.IO.Path.GetDirectoryName(store.FilePath)], _files.CreatedDirectories);

    _files.FailWrites = true;
    _files.Attempts = 0;
    Assert.False(store.Save());
    Assert.Single(_log.Warnings);
    Assert.Equal(5, _files.Attempts);
  }

  [Fact]
  public void SaveRetriesAWriteTheEditorBlocked()
  {
    var store = NewStore();
    store.RecordOpened("/home/a/Proj");
    _files.FailNextWrites = 2;

    Assert.True(store.Save());

    Assert.Equal(3, _files.Attempts);
    Assert.Empty(_log.Warnings);
    Assert.Single(Saved()["projects"]!.AsArray());
  }

  [Theory]
  [InlineData("{\"version\":1,\"version\":2,\"projects\":[]}")]
  [InlineData("{\"projects\":[{\"path\":\"/home/a/A\",\"path\":\"/home/a/B\"}]}")]
  public void ARepeatedKeyGivesAnEmptyListWithOneWarning(string text)
  {
    _files.Seed(FilePath, text);

    var store = NewStore();

    Assert.Empty(store.Entries());
    Assert.Single(_log.Warnings);
  }

  [Fact]
  public void RecordingAnEmptyPathChangesNothing()
  {
    var store = NewStore();
    store.RecordOpened("/home/a/Proj");

    store.RecordOpened("");

    Assert.Equal(["/home/a/Proj"], Paths(store));
  }

  [Theory]
  [InlineData("this is { not json")]
  [InlineData("[1,2]")]
  [InlineData("{\"projects\": 5}")]
  [InlineData("{\"projects\": null}")]
  public void UnreadableFileGivesAnEmptyListWithOneWarningAndIsReplacedOnSave(string text)
  {
    _files.Seed(FilePath, text);

    var store = NewStore();

    Assert.Empty(store.Entries());
    Assert.Single(_log.Warnings);

    store.RecordOpened("/home/a/Recovered");
    Assert.True(store.Save());
    Assert.Single(Saved()["projects"]!.AsArray());
  }

  [Fact]
  public void MalformedEntriesAreDroppedAndValidNeighborsKept()
  {
    _files.Seed(FilePath,
      "{\"version\":1,\"projects\":[5,{\"name\":\"no path\"},{\"path\":7},{\"path\":\"/home/a/Good\",\"name\":\"Good\"}]}");

    var store = NewStore();

    Assert.Equal(["/home/a/Good"], Paths(store));
    Assert.Single(_log.Warnings);
  }

  [Fact]
  public void UnknownFieldsSurviveBothTopLevelAndPerEntry()
  {
    _files.Seed(FilePath,
      "{\"version\":3,\"futureFeature\":{\"x\":[1,2]},\"projects\":"
      + "[{\"path\":\"/home/a/Proj\",\"name\":\"Proj\",\"color\":\"red\",\"tags\":[1]}]}");

    var store = NewStore();
    store.RecordOpened("/home/a/Proj");
    store.SetPinned("/home/a/Proj", true);
    Assert.True(store.Save());

    var saved = Saved();
    var entry = saved["projects"]![0]!;
    Assert.Equal(3, (int)saved["version"]!);
    Assert.Equal("[1,2]", saved["futureFeature"]!["x"]!.ToJsonString());
    Assert.Equal("red", (string)entry["color"]!);
    Assert.Equal("[1]", entry["tags"]!.ToJsonString());
    Assert.True((bool)entry["pinned"]!);
  }

  [Fact]
  public void AFileWithNoProjectsKeyKeepsItsOtherFields()
  {
    _files.Seed(FilePath, "{\"version\":1,\"futureFeature\":true}");

    var store = NewStore();
    store.RecordOpened("/home/a/Proj");
    Assert.True(store.Save());

    Assert.True((bool)Saved()["futureFeature"]!);
    Assert.Empty(_log.Warnings);
  }

  [Fact]
  public void RecordingStampsTheTimeInTheSharedFormatAndNamesTheFolder()
  {
    _time.Now = new DateTimeOffset(2026, 10, 18, 19, 2, 11, 987, TimeSpan.Zero);
    var store = NewStore();

    store.RecordOpened("/home/a/Folder Project");
    store.RecordOpened("/home/a/legacy/Game.JSON");

    var entries = store.Entries();
    Assert.Equal("Game", entries[0].Name);
    Assert.Equal("Folder Project", entries[1].Name);
    Assert.Equal("2026-10-18T19:02:11Z", entries[0].LastOpenedUtc);
  }

  [Theory]
  [InlineData("/home/a/Proj/project.ecs3d", "/home/a/Proj")]
  [InlineData("/home/a/Proj/other.ECS3D", "/home/a/Proj")]
  [InlineData("/home/a/Proj/", "/home/a/Proj")]
  [InlineData("/home/a//x/../Proj/./", "/home/a/Proj")]
  [InlineData("/home/a/Game.json", "/home/a/Game.json")]
  [InlineData("/", "/")]
  [InlineData("//home//a/Proj", "/home/a/Proj")]
  [InlineData("/home/a/Proj.ecs3d/", "/home/a/Proj.ecs3d")]
  public void PathsAreRecordedNormalized(string given, string expected)
  {
    var store = NewStore();

    store.RecordOpened(given);

    Assert.Equal([expected], Paths(store));
  }

  [Fact]
  public void WindowsPathsAreNormalizedToForwardSlashesAndMatchTheirForwardSlashForm()
  {
    var store = NewStore(HostOs.Windows);

    store.RecordOpened(@"C:\Users\a\Proj");
    store.RecordOpened("c:/users/A/proj/project.ecs3d");
    store.RecordOpened(@"C:\");

    Assert.Equal(["C:/", "c:/users/A/proj"], Paths(store));
  }

  [Fact]
  public void AUncShareIsTheRootSoParentSegmentsCannotClimbPastIt()
  {
    var store = NewStore(HostOs.Windows);

    store.RecordOpened("//server/share/../../x/Proj");

    Assert.Equal(["//server/share/x/Proj"], Paths(store));
  }

  [Fact]
  public void ReopeningMovesTheEntryToTheFrontKeepingPinAndTemplate()
  {
    var store = NewStore();
    store.RecordOpened("/home/a/One", "sample");
    store.SetPinned("/home/a/One", true);
    _time.Now = _time.Now.AddMinutes(5);
    store.RecordOpened("/home/a/Two");
    _time.Now = _time.Now.AddMinutes(5);

    store.RecordOpened("/home/a/One");

    var entries = store.Entries();
    Assert.Equal(["/home/a/One", "/home/a/Two"], Paths(store));
    Assert.True(entries[0].Pinned);
    Assert.Equal("sample", entries[0].Template);
    Assert.Equal("2026-10-18T19:12:11Z", entries[0].LastOpenedUtc);
  }

  [Fact]
  public void AnOpenWithoutATemplateNeverClearsOne()
  {
    var store = NewStore();
    store.RecordOpened("/home/a/One", "first-person");

    store.RecordOpened("/home/a/One");
    Assert.Equal("first-person", store.Entries()[0].Template);

    store.RecordOpened("/home/a/One", "coin-rush");
    Assert.Equal("coin-rush", store.Entries()[0].Template);
  }

  [Theory]
  [InlineData(HostOs.Windows, "C:/Users/a/Proj", "c:/USERS/a/proj", 1)]
  [InlineData(HostOs.MacOs, "/Users/a/Proj", "/users/A/PROJ", 1)]
  [InlineData(HostOs.Linux, "/home/a/Proj", "/home/a/proj", 2)]
  public void PathsDedupeCaseInsensitivelyOnWindowsAndMacButNotOnLinux(
    HostOs os, string first, string second, int expectedCount)
  {
    var store = NewStore(os);

    store.RecordOpened(first);
    store.RecordOpened(second);

    Assert.Equal(expectedCount, store.Entries().Count);
    Assert.Equal(second, store.Entries()[0].Path);
  }

  private void FillTo(RecentProjectsStore store, int count)
  {
    for (var i = 0; i < count; i++)
    {
      store.RecordOpened($"/home/a/P{i}");
      _time.Now = _time.Now.AddMinutes(1);
    }
  }

  [Fact]
  public void TheCapEvictsTheOldestUnpinnedEntry()
  {
    var store = NewStore();
    FillTo(store, RecentProjectsStore.MaxEntries);
    store.SetPinned("/home/a/P0", true);

    store.RecordOpened("/home/a/New");

    var paths = Paths(store);
    Assert.Equal(RecentProjectsStore.MaxEntries, paths.Count);
    Assert.Equal("/home/a/New", paths[0]);
    Assert.Contains("/home/a/P0", paths);
    Assert.DoesNotContain("/home/a/P1", paths);
    Assert.Contains("/home/a/P2", paths);
  }

  [Fact]
  public void TheCapNeverEvictsTheEntryJustRecordedOrPinnedOnes()
  {
    var store = NewStore();
    FillTo(store, RecentProjectsStore.MaxEntries);
    for (var i = 0; i < RecentProjectsStore.MaxEntries; i++)
    {
      store.SetPinned($"/home/a/P{i}", true);
    }

    _time.Now = new DateTimeOffset(2000, 1, 1, 0, 0, 0, TimeSpan.Zero);
    store.RecordOpened("/home/a/New");

    Assert.Equal(RecentProjectsStore.MaxEntries + 1, store.Entries().Count);
    Assert.Equal("/home/a/New", store.Entries()[0].Path);
  }

  [Fact]
  public void AnEvictionTieGoesToTheLaterListEntry()
  {
    var entries = Enumerable.Range(0, RecentProjectsStore.MaxEntries)
      .Select(i => $"{{\"path\":\"/home/a/P{i}\",\"lastOpenedUtc\":\"2026-01-01T00:00:00Z\"}}");
    _files.Seed(FilePath, "{\"projects\":[" + string.Join(",", entries) + "]}");
    var store = NewStore();

    store.RecordOpened("/home/a/New");

    var paths = Paths(store);
    Assert.DoesNotContain("/home/a/P49", paths);
    Assert.Contains("/home/a/P48", paths);
    Assert.Contains("/home/a/P0", paths);
  }

  [Fact]
  public void AnUnparseableTimeCountsAsOldestForEviction()
  {
    var store = NewStore();
    FillTo(store, RecentProjectsStore.MaxEntries);
    Assert.True(store.Save());
    var saved = Saved();
    saved["projects"]![10]!["lastOpenedUtc"] = "yesterday";
    _files.Seed(FilePath, saved.ToJsonString());
    store = NewStore();

    store.RecordOpened("/home/a/New");

    var paths = Paths(store);
    Assert.DoesNotContain("/home/a/P39", paths);
    Assert.Contains("/home/a/P0", paths);
  }

  [Fact]
  public void PinAndUnpinToggleOnlyThatEntry()
  {
    var store = NewStore();
    store.RecordOpened("/home/a/One");
    store.RecordOpened("/home/a/Two");

    Assert.True(store.SetPinned("/home/a/One", true));
    Assert.Equal([false, true], store.Entries().Select(e => e.Pinned).ToList());

    Assert.True(store.SetPinned("/home/a/One", false));
    Assert.All(store.Entries(), e => Assert.False(e.Pinned));
    Assert.False(store.SetPinned("/home/a/Unknown", true));
  }

  [Fact]
  public void RemoveDropsTheEntryAndTouchesNoFiles()
  {
    _files.AddFile("/home/a/One/project.ecs3d");
    var store = NewStore();
    store.RecordOpened("/home/a/One");
    store.RecordOpened("/home/a/Two");

    Assert.True(store.Remove("/home/a/One/"));
    Assert.False(store.Remove("/home/a/One"));

    Assert.Equal(["/home/a/Two"], Paths(store));
    Assert.True(_files.FileExists("/home/a/One/project.ecs3d"));
    Assert.Equal(0, _files.Writes);
  }

  [Fact]
  public void RelocateRepointsTheEntryKeepingPositionPinTemplateAndUnknownFields()
  {
    _files.Seed(FilePath,
      "{\"projects\":[{\"path\":\"/home/a/Old\",\"name\":\"Old\",\"lastOpenedUtc\":\"2026-01-02T03:04:05Z\","
      + "\"pinned\":true,\"template\":\"sample\",\"color\":\"red\"},{\"path\":\"/home/a/Other\"}]}");
    var store = NewStore();

    Assert.True(store.Relocate("/home/a/Old", "/mnt/moved/Renamed"));
    Assert.True(store.Save());

    var entries = store.Entries();
    Assert.Equal(new RecentProject("/mnt/moved/Renamed", "Renamed", "2026-01-02T03:04:05Z", true, "sample"), entries[0]);
    Assert.Equal("/home/a/Other", entries[1].Path);
    Assert.Equal("red", (string)Saved()["projects"]![0]!["color"]!);
    Assert.False(store.Relocate("/home/a/Gone", "/mnt/x"));
  }

  [Fact]
  public void RelocateOntoAnExistingEntryMergesThemKeepingPins()
  {
    var store = NewStore();
    store.RecordOpened("/home/a/Target");
    store.SetPinned("/home/a/Target", true);
    store.RecordOpened("/home/a/Old");

    Assert.True(store.Relocate("/home/a/Old", "/home/a/Target"));

    var entries = store.Entries();
    Assert.Single(entries);
    Assert.Equal("/home/a/Target", entries[0].Path);
    Assert.True(entries[0].Pinned);
  }

  [Fact]
  public void RelocateOntoAnEntryKeepsTheNewerTimeAndFillsAMissingTemplate()
  {
    _files.Seed(FilePath,
      "{\"projects\":[{\"path\":\"/a/Old\",\"lastOpenedUtc\":\"2026-01-01T00:00:00Z\"},"
      + "{\"path\":\"/a/Target\",\"lastOpenedUtc\":\"2026-02-01T00:00:00Z\",\"template\":\"sample\"}]}");
    var store = NewStore();

    Assert.True(store.Relocate("/a/Old", "/a/Target"));

    var entry = Assert.Single(store.Entries());
    Assert.Equal("2026-02-01T00:00:00Z", entry.LastOpenedUtc);
    Assert.Equal("sample", entry.Template);
  }

  [Fact]
  public void TheEditorsFixtureDocumentLoadsCorrectly()
  {
    _files.Seed(FilePath,
      "{\"version\":1,\"projects\":[5,{\"name\":\"no path\"},{\"path\":7},"
      + "{\"path\":\"/home/a/Good\",\"name\":\"Good\",\"lastOpenedUtc\":\"2026-10-18T19:02:11Z\",\"pinned\":true,"
      + "\"template\":\"first-person\"}]}");

    var entries = NewStore().Entries();

    Assert.Equal(new RecentProject("/home/a/Good", "Good", "2026-10-18T19:02:11Z", true, "first-person"), Assert.Single(entries));
  }
}
