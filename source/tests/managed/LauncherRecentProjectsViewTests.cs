using System;
using System.Linq;
using ECS3DLauncher.Services;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherRecentProjectsViewTests
{
  private static readonly DateTimeOffset Now = new(2026, 10, 18, 19, 2, 11, TimeSpan.Zero);

  private readonly FakeFileSystem _files = new();

  private static RecentProject Entry(string name, string time, bool pinned) =>
    new($"/home/a/{name}", name, time, pinned, null);

  private static RecentProject At(string path) => new(path, "name", "", false, null);

  private static string EditedAgo(int seconds) =>
    RecentProjectsView.EditedText(RecentProjectTime.Write(Now.AddSeconds(-seconds)), Now);

  [Fact]
  public void SortPutsPinnedFirstThenNewestAndUnparseableLast()
  {
    var sorted = RecentProjectsView.Sort([
      Entry("old", "2026-01-01T00:00:00Z", false),
      Entry("bad", "garbage", false),
      Entry("new", "2026-03-01T00:00:00Z", false),
      Entry("pinnedOld", "2025-01-01T00:00:00Z", true),
      Entry("pinnedNew", "2025-06-01T00:00:00Z", true),
    ]);

    Assert.Equal(["pinnedNew", "pinnedOld", "new", "old", "bad"], sorted.Select(e => e.Name).ToList());
  }

  [Fact]
  public void FilterMatchesNameOrPathIgnoringCaseAndTrimsTheQuery()
  {
    var entries = new[]
    {
      new RecentProject("/work/one", "Courtyard", "", false, null),
      new RecentProject("/games/alpha", "Zed", "", false, null),
    };

    Assert.Equal(["Courtyard"], RecentProjectsView.Filter(entries, "COURT").Select(e => e.Name).ToList());
    Assert.Equal(["Zed"], RecentProjectsView.Filter(entries, "/GAMES").Select(e => e.Name).ToList());
    Assert.Equal(["Courtyard"], RecentProjectsView.Filter(entries, "  court  ").Select(e => e.Name).ToList());
    Assert.Empty(RecentProjectsView.Filter(entries, "zzz"));
  }

  [Theory]
  [InlineData(null)]
  [InlineData("")]
  [InlineData("   ")]
  public void ABlankQueryKeepsEverythingInOrder(string? query)
  {
    var entries = new[] { Entry("bravo", "", false), Entry("alpha", "", false) };

    Assert.Equal(["bravo", "alpha"], RecentProjectsView.Filter(entries, query).Select(e => e.Name).ToList());
    Assert.Equal(["alpha"], RecentProjectsView.Filter(entries, "alp").Select(e => e.Name).ToList());
  }

  [Fact]
  public void AFolderWithAProjectFileIsNotMissing()
  {
    _files.AddDirectory("/home/a/Here");
    _files.Seed("/home/a/Here/project.ecs3d", "{}");

    Assert.False(RecentProjectsView.IsMissing(_files, At("/home/a/Here")));
    Assert.False(RecentProjectsView.IsMissing(_files, At("/home/a/Here/")));
    Assert.True(RecentProjectsView.IsMissing(_files, At("/home/a/Gone")));
  }

  [Fact]
  public void AFolderWithoutAProjectFileIsMissing()
  {
    _files.AddDirectory("/home/a/NoFile");
    _files.AddDirectory("/home/a/Here");
    _files.Seed("/home/a/Here/project.ecs3d", "{}");

    Assert.True(RecentProjectsView.IsMissing(_files, At("/home/a/NoFile")));
    Assert.False(RecentProjectsView.IsMissing(_files, At("/home/a/Here")));
  }

  [Fact]
  public void ALegacyFileIsMissingOnlyWhenTheFileIsGone()
  {
    _files.Seed("/home/a/Game.json", "{}");
    _files.Seed("/home/a/Upper.JSON", "{}");

    Assert.False(RecentProjectsView.IsMissing(_files, At("/home/a/Game.json")));
    Assert.True(RecentProjectsView.IsMissing(_files, At("/home/a/Old.json")));
    Assert.False(RecentProjectsView.IsMissing(_files, At("/home/a/Upper.JSON")));
    Assert.True(RecentProjectsView.IsMissing(_files, At("/home/a/Gone.JSON")));
  }

  [Theory]
  [InlineData(0, "just now")]
  [InlineData(59, "just now")]
  [InlineData(60, "1 minute ago")]
  [InlineData(12 * 60, "12 minutes ago")]
  [InlineData(59 * 60 + 59, "59 minutes ago")]
  [InlineData(3600, "1 hour ago")]
  [InlineData(90 * 60, "1 hour ago")]
  [InlineData(5 * 3600, "5 hours ago")]
  [InlineData(24 * 3600 - 1, "23 hours ago")]
  [InlineData(24 * 3600, "Yesterday")]
  [InlineData(48 * 3600 - 1, "Yesterday")]
  [InlineData(48 * 3600, "2 days ago")]
  [InlineData(7 * 24 * 3600 - 1, "6 days ago")]
  [InlineData(7 * 24 * 3600, "Oct 11, 2026")]
  [InlineData(400 * 24 * 3600, "Sep 13, 2025")]
  public void EditedTextBoundaries(int secondsAgo, string expected)
  {
    Assert.Equal(expected, EditedAgo(secondsAgo));
  }

  [Theory]
  [InlineData(-30)]
  [InlineData(-3600)]
  [InlineData(-30 * 24 * 3600)]
  public void AFutureTimeReadsAsJustNow(int secondsAgo)
  {
    Assert.Equal("just now", EditedAgo(secondsAgo));
    Assert.Equal("1 minute ago", EditedAgo(60));
  }

  [Fact]
  public void EditedTextForAnUnreadableTimeIsUnknown()
  {
    Assert.Equal("Unknown", RecentProjectsView.EditedText("", Now));
    Assert.Equal("Unknown", RecentProjectsView.EditedText("2026-02-30T00:00:00Z", Now));
    Assert.Equal("Unknown", RecentProjectsView.EditedText("2026-10-18 19:02:11", Now));
    Assert.Equal("just now", RecentProjectsView.EditedText("2026-10-18T19:02:11Z", Now));
  }

  [Fact]
  public void ClassifyAcceptsAProjectFolderItsProjectFileAndALegacyJson()
  {
    _files.AddDirectory("/home/a/Proj");
    _files.Seed("/home/a/Proj/project.ecs3d", "{}");
    _files.Seed("/home/a/Game.json", "{}");

    var folder = ProjectPathKind.Classify(_files, "/home/a/Proj");
    var projectFile = ProjectPathKind.Classify(_files, "/home/a/Proj/project.ecs3d");
    var legacy = ProjectPathKind.Classify(_files, "/home/a/Game.json");

    Assert.Equal(new ProjectPathResult(ProjectPathType.Folder, "/home/a/Proj", null), folder);
    Assert.Equal(new ProjectPathResult(ProjectPathType.ProjectFile, "/home/a/Proj", null), projectFile);
    Assert.Equal(new ProjectPathResult(ProjectPathType.LegacyFile, "/home/a/Game.json", null), legacy);
  }

  [Fact]
  public void ClassifyAcceptsATrailingSlashAndAnUpperCaseJsonExtension()
  {
    _files.AddDirectory("/home/a/Proj/");
    _files.Seed("/home/a/Proj/project.ecs3d", "{}");
    _files.Seed("/home/a/Game.JSON", "{}");
    _files.Seed("/home/a/Game.jsonx", "{}");

    Assert.Equal(ProjectPathType.Folder, ProjectPathKind.Classify(_files, "/home/a/Proj/").Type);
    Assert.Equal(ProjectPathType.LegacyFile, ProjectPathKind.Classify(_files, "/home/a/Game.JSON").Type);
    Assert.Equal(ProjectPathType.Invalid, ProjectPathKind.Classify(_files, "/home/a/Game.jsonx").Type);
  }

  [Theory]
  [InlineData("")]
  [InlineData("   ")]
  [InlineData("/home/a/Nothing")]
  [InlineData("/home/a/Empty")]
  [InlineData("/home/a/notes.txt")]
  [InlineData("/home/a/other.ecs3d")]
  public void ClassifyRejectsAnythingElseWithAMessage(string path)
  {
    _files.AddDirectory("/home/a/Empty");
    _files.AddDirectory("/home/a/Proj");
    _files.Seed("/home/a/Proj/project.ecs3d", "{}");
    _files.Seed("/home/a/notes.txt", "x");
    _files.Seed("/home/a/other.ecs3d", "{}");

    var result = ProjectPathKind.Classify(_files, path);

    Assert.Equal(ProjectPathType.Invalid, result.Type);
    Assert.Equal(string.Empty, result.Path);
    Assert.False(string.IsNullOrEmpty(result.Error));
    Assert.Equal(ProjectPathType.Folder, ProjectPathKind.Classify(_files, "/home/a/Proj").Type);
  }

  [Theory]
  [InlineData("C:/project.ecs3d", "C:/")]
  [InlineData("C:\\project.ecs3d", "C:\\")]
  [InlineData("/project.ecs3d", "/")]
  [InlineData("C:\\Games\\Proj\\project.ecs3d", "C:\\Games\\Proj")]
  [InlineData("PROJECT.ECS3D", ".")]
  public void ClassifyReturnsTheFolderOfAProjectFileKeepingARootSeparator(string file, string folder)
  {
    _files.Seed(file, "{}");

    Assert.Equal(new ProjectPathResult(ProjectPathType.ProjectFile, folder, null), ProjectPathKind.Classify(_files, file));
  }
}
