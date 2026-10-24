using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using Avalonia.Media.Imaging;
using ECS3DLauncher.Services;
using ECS3DLauncher.Shell;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherSamplesTabTests
{
  private readonly LauncherRig _rig = new();
  private readonly FakeProcessRunner _runner = new();

  private static string TemplatesOf(string engineFolder) => Path.Combine(Path.GetFullPath(engineFolder), "templates");

  private static string FolderOf(string engineFolder, string id) => Path.Combine(TemplatesOf(engineFolder), id);

  private static string Manifest(string id, string name, string category, string description, string tags, string extra) =>
    "{\"id\":\"" + id + "\",\"name\":\"" + name + "\",\"description\":\"" + description + "\",\"category\":\""
    + category + "\",\"tags\":[" + tags + "],\"thumbnail\":\"thumbnail.png\",\"entryScene\":\"Main\","
    + "\"minEngineVersion\":\"0.0.1\"" + extra + "}";

  private void AddEntry(string engineFolder, string id, string json)
  {
    _rig.Files.AddDirectory(TemplatesOf(engineFolder));
    _rig.Files.AddDirectory(FolderOf(engineFolder, id));
    _rig.Files.Seed(Path.Combine(FolderOf(engineFolder, id), "template.json"), json);
  }

  private void AddSample(string engineFolder, string id, string name, string description, string tags, string extra = "") =>
    AddEntry(engineFolder, id, Manifest(id, name, "sample", description, tags, extra));

  private void AddStandardEntries(string engineFolder)
  {
    AddSample(engineFolder, "rush", "Coin Rush", "Grab every coin", "\"Arcade\",\"Co-op\"", ",\"players\":{\"min\":1,\"max\":4}");
    AddSample(engineFolder, "playground", "Physics Playground", "Push boxes around", "\"physics\"");
    AddEntry(engineFolder, "blank", Manifest("blank", "Blank Template", "template", "Start empty", "\"empty\"", ""));
  }

  private MainWindowViewModel NewMain(Func<string, Bitmap?>? loadThumbnail = null) =>
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
    model.Samples.VisibleSamples.Select(card => card.Name).ToArray();

  [Fact]
  public void OnlySamplesBecomeCards()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddStandardEntries(LauncherRig.LauncherDir);

    var model = NewMain();

    Assert.Equal(new[] { "Coin Rush", "Physics Playground" }, model.Samples.Samples.Select(card => card.Name).ToArray());
    Assert.DoesNotContain(model.Samples.Samples, card => card.Name == "Blank Template");
    Assert.False(model.Samples.IsEmpty);
  }

  [Fact]
  public void CardFieldsComeFromTheManifest()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddStandardEntries(LauncherRig.LauncherDir);

    var cards = NewMain().Samples.Samples;
    var rush = cards.Single(card => card.Name == "Coin Rush");
    var playground = cards.Single(card => card.Name == "Physics Playground");

    Assert.Equal("Grab every coin", rush.Description);
    Assert.Equal(new[] { "Arcade", "Co-op" }, rush.Tags.ToArray());
    Assert.Equal(FolderOf(LauncherRig.LauncherDir, "rush"), rush.FolderPath);
    Assert.Equal("1-4 players", rush.PlayersLabel);
    Assert.True(rush.HasPlayers);
    Assert.Null(playground.PlayersLabel);
    Assert.False(playground.HasPlayers);
    Assert.False(playground.HasThumbnail);
  }

  [Fact]
  public void OnlyAMultiplayerRangeShowsAPlayersPill()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddSample(LauncherRig.LauncherDir, "solo", "Solo", "Alone", "\"a\"", ",\"players\":{\"min\":1,\"max\":1}");
    AddSample(LauncherRig.LauncherDir, "duo", "Duo", "Pair", "\"a\"", ",\"players\":{\"min\":2,\"max\":2}");

    var cards = NewMain().Samples.Samples;

    Assert.Null(cards.Single(card => card.Name == "Solo").PlayersLabel);
    Assert.Equal("2-2 players", cards.Single(card => card.Name == "Duo").PlayersLabel);
  }

  [Theory]
  [InlineData("coin", "Coin Rush")]
  [InlineData("PUSH BOXES", "Physics Playground")]
  [InlineData("co-op", "Coin Rush")]
  [InlineData("PHYSICS", "Physics Playground")]
  public void SearchMatchesNameDescriptionAndTagsIgnoringCase(string query, string expected)
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddStandardEntries(LauncherRig.LauncherDir);
    var model = NewMain();
    Assert.Equal(2, Names(model).Length);

    model.SearchText = query;

    Assert.Equal(new[] { expected }, Names(model));
  }

  [Fact]
  public void ASearchWithNoMatchShowsTheNoMatchesState()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddStandardEntries(LauncherRig.LauncherDir);
    var model = NewMain();
    Assert.False(model.Samples.HasNoMatches);

    model.SearchText = "zzz-nothing";

    Assert.Empty(Names(model));
    Assert.True(model.Samples.HasNoMatches);
    Assert.True(model.Samples.ShowEmptyState);
    Assert.Equal("No samples match your search", model.Samples.EmptyHeading);

    model.SearchText = "";

    Assert.Equal(2, Names(model).Length);
    Assert.False(model.Samples.ShowEmptyState);
  }

  [Fact]
  public void NoSamplesExplainWhyTheTabIsEmpty()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);

    var missing = NewMain().Samples;

    Assert.True(missing.IsEmpty);
    Assert.True(missing.ShowEmptyState);
    Assert.False(missing.HasNoMatches);
    Assert.Equal($"No templates folder found at {TemplatesOf(LauncherRig.LauncherDir)}", missing.EmptyHeading);

    AddEntry(LauncherRig.LauncherDir, "blank", Manifest("blank", "Blank", "template", "d", "\"a\"", ""));

    Assert.Equal(
      $"No samples found in {TemplatesOf(LauncherRig.LauncherDir)}",
      NewMain().Samples.EmptyHeading);
  }

  [Fact]
  public void AnUnreadableTemplatesFolderIsNamed()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddStandardEntries(LauncherRig.LauncherDir);
    _rig.Files.FailEnumerate = true;

    var samples = NewMain().Samples;

    Assert.True(samples.IsEmpty);
    Assert.Equal($"Could not read {TemplatesOf(LauncherRig.LauncherDir)}", samples.EmptyHeading);
  }

  [Fact]
  public void TheSamplesNavCountIsTheNumberOfSamples()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddStandardEntries(LauncherRig.LauncherDir);

    var model = NewMain();

    Assert.Equal("2", model.NavItems.Single(item => item.Key == "samples").Count);
  }

  [Fact]
  public void SearchTextInTheShellFiltersTheSamples()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddStandardEntries(LauncherRig.LauncherDir);
    var model = NewMain();

    model.SearchText = "arcade";

    Assert.Equal("arcade", model.Samples.SearchText);
    Assert.Equal(new[] { "Coin Rush" }, Names(model));
  }

  [Fact]
  public void PlayLaunchesTheClientWithTheShippedFolder()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddStandardEntries(LauncherRig.LauncherDir);
    var model = NewMain();
    var card = model.Samples.Samples.Single(item => item.Name == "Coin Rush");

    Assert.True(model.Samples.PlayCommand.CanExecute(card));
    Assert.Null(model.Samples.PlayTooltip);
    model.Samples.PlayCommand.Execute(card);

    var launch = Assert.Single(_runner.Launches);
    Assert.Equal(Path.Combine(Path.GetFullPath(LauncherRig.LauncherDir), "ECS3DClient"), launch.FileName);
    Assert.Equal(new[] { "--project", FolderOf(LauncherRig.LauncherDir, "rush") }, launch.Arguments.ToArray());
    Assert.Equal(Path.GetFullPath(LauncherRig.LauncherDir), launch.WorkingDirectory);
  }

  [Fact]
  public void PlayIsUnavailableWithoutTheClientAndTheTooltipNamesIt()
  {
    _rig.AddEngine(LauncherRig.LauncherDir, EngineExecutable.Client);
    AddStandardEntries(LauncherRig.LauncherDir);
    var model = NewMain();
    var card = model.Samples.Samples[0];

    Assert.False(model.Samples.PlayCommand.CanExecute(card));
    Assert.Contains("ECS3DClient", model.Samples.PlayTooltip);
    model.Samples.PlayCommand.Execute(card);

    Assert.Empty(_runner.Launches);
  }

  [Fact]
  public void ChangingTheEngineFolderReloadsTheSamplesAndPlay()
  {
    _rig.AddEngine(LauncherRig.LauncherDir, EngineExecutable.Client);
    AddStandardEntries(LauncherRig.LauncherDir);
    _rig.AddEngine(LauncherRig.OtherDir);
    AddSample(LauncherRig.OtherDir, "only", "Only One", "The sole sample", "\"x\"");
    var model = NewMain();
    Assert.Equal(2, model.Samples.Samples.Count);
    Assert.NotNull(model.Samples.PlayTooltip);

    model.Settings.EngineFolder = LauncherRig.OtherDir;

    Assert.Equal(new[] { "Only One" }, model.Samples.Samples.Select(card => card.Name).ToArray());
    Assert.Equal("1", model.NavItems.Single(item => item.Key == "samples").Count);
    Assert.Null(model.Samples.PlayTooltip);
    Assert.True(model.Samples.PlayCommand.CanExecute(model.Samples.Samples[0]));
    model.Samples.PlayCommand.Execute(model.Samples.Samples[0]);
    Assert.Equal(
      new[] { "--project", FolderOf(LauncherRig.OtherDir, "only") },
      Assert.Single(_runner.Launches).Arguments.ToArray());
  }

  [Fact]
  public void OnlyASampleWithAThumbnailFileCallsTheLoader()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddStandardEntries(LauncherRig.LauncherDir);
    var thumbnail = Path.Combine(FolderOf(LauncherRig.LauncherDir, "rush"), "thumbnail.png");
    _rig.Files.Seed(thumbnail, "png");
    var requested = new List<string>();

    var cards = NewMain(path =>
    {
      requested.Add(path);
      return null;
    }).Samples.Samples;

    Assert.Equal(new[] { thumbnail }, requested.ToArray());
    Assert.False(cards.Single(card => card.Name == "Coin Rush").HasThumbnail);
    Assert.False(cards.Single(card => card.Name == "Physics Playground").HasThumbnail);
  }

  [Fact]
  public void ChangingTheEngineFolderRaisesPlayCanExecuteChanged()
  {
    _rig.AddEngine(LauncherRig.LauncherDir, EngineExecutable.Client);
    _rig.AddEngine(LauncherRig.OtherDir);
    var model = NewMain();
    var changes = 0;
    model.Samples.PlayCommand.CanExecuteChanged += (_, _) => changes++;

    model.Settings.EngineFolder = LauncherRig.OtherDir;

    Assert.True(changes > 0);
  }

  [Fact]
  public void AShellSearchSurvivesAnEngineFolderReload()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddStandardEntries(LauncherRig.LauncherDir);
    _rig.AddEngine(LauncherRig.OtherDir);
    AddStandardEntries(LauncherRig.OtherDir);
    var model = NewMain();
    model.SearchText = "arcade";
    Assert.Equal(new[] { "Coin Rush" }, Names(model));

    model.Settings.EngineFolder = LauncherRig.OtherDir;

    Assert.Equal(new[] { "Coin Rush" }, Names(model));
    Assert.Equal(FolderOf(LauncherRig.OtherDir, "rush"), model.Samples.VisibleSamples[0].FolderPath);
    Assert.Equal(2, model.Samples.Samples.Count);
  }

  [Fact]
  public async Task CoordinatorPlayRunsTheClientInTheEngineFolder()
  {
    var runner = new FakeProcessRunner();
    var recorder = new NoticeRecorder();
    var engine = ShellFixtures.EngineWithout();
    var coordinator = ShellFixtures.CoordinatorFor(engine, runner, TimeSpan.FromHours(1), recorder);

    var watch = coordinator.PlayProjectAsync("/engine/templates/rush");
    runner.Last!.Exit(0);
    await watch;

    var launch = Assert.Single(runner.Launches);
    Assert.Equal(engine.Found[EngineExecutable.Client], launch.FileName);
    Assert.Equal(new[] { "--project", "/engine/templates/rush" }, launch.Arguments.ToArray());
    Assert.Equal(ShellFixtures.EngineFolder, launch.WorkingDirectory);
    Assert.Empty(recorder.Notices);
  }

  [Fact]
  public async Task CoordinatorPlayToastsAFailedStartAndAnEarlyExitWithTheClientLog()
  {
    var failing = new FakeProcessRunner { FailWith = "access denied" };
    var failedRecorder = new NoticeRecorder();
    var coordinator = ShellFixtures.CoordinatorFor(
      ShellFixtures.EngineWithout(), failing, TimeSpan.FromHours(1), failedRecorder);

    await coordinator.PlayProjectAsync("/p");

    Assert.Contains("access denied", Assert.Single(failedRecorder.Notices).Message);

    var runner = new FakeProcessRunner();
    var exitRecorder = new NoticeRecorder();
    var exiting = ShellFixtures.CoordinatorFor(
      ShellFixtures.EngineWithout(), runner, TimeSpan.FromHours(1), exitRecorder);

    var watch = exiting.PlayProjectAsync("/p");
    runner.Last!.Exit(3);
    await watch;

    var notice = Assert.Single(exitRecorder.Notices);
    Assert.Equal(ShellFixtures.ExpectedLog("client"), notice.LogPath);
    Assert.Contains("ECS3DClient", notice.Message);
  }

  // The catalog reads the rig's file system; the shell and creator use the separate in-memory tree that holds the copy.
  private MainWindowViewModel NewCopyMain(MemoryTreeFileSystem files)
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddStandardEntries(LauncherRig.LauncherDir);
    var folder = FolderOf(LauncherRig.LauncherDir, "rush");
    files.Seed(Path.Combine(folder, "template.json"), "{}");
    files.Seed(Path.Combine(folder, "project.ecs3d"), "{\"assets\":{\"scenes\":[{\"objects\":[{\"uuid\":\"a\",\"children\":[]}]}]}}");
    var counter = 0;
    return new MainWindowViewModel(
      _rig.NewEditor(),
      _rig.NewRecents(),
      _runner,
      new EarlyExitWatcher(ShellFixtures.UserData, TimeSpan.FromHours(1)),
      _rig.NewCatalog(),
      _ => null,
      "1.2.3",
      action => action(),
      () => _rig.Time.Now,
      files,
      work => work(),
      new ProjectCreator(files, _rig.Log, () => new Guid($"00000000-0000-0000-0000-{++counter:D12}")));
  }

  [Fact]
  public void OpenACopyOpensNewProjectNamedAfterTheSample()
  {
    var model = NewCopyMain(new MemoryTreeFileSystem());
    var card = model.Samples.Samples.Single(item => item.Name == "Coin Rush");

    Assert.Equal("rush", card.Id);
    Assert.True(model.Samples.OpenCopyCommand.CanExecute(card));
    model.Samples.OpenCopyCommand.Execute(card);

    Assert.NotNull(model.NewProject);
    Assert.Equal("Coin Rush", model.NewProject.ProjectName);
    Assert.False(model.IsInteractive);
  }

  [Fact]
  public void OpenACopyDoesNothingForNoCardOrWhileTheDialogIsOpen()
  {
    var model = NewCopyMain(new MemoryTreeFileSystem());
    model.Samples.OpenCopyCommand.Execute(null);
    Assert.Null(model.NewProject);

    model.Samples.OpenCopyCommand.Execute(model.Samples.Samples.Single(item => item.Name == "Coin Rush"));
    var first = model.NewProject;
    Assert.NotNull(first);

    model.Samples.OpenCopyCommand.Execute(model.Samples.Samples.Single(item => item.Name == "Physics Playground"));

    Assert.Same(first, model.NewProject);
    Assert.Equal("Coin Rush", model.NewProject!.ProjectName);
  }

  [Fact]
  public void CreatingACopyWithTheEditorBoxCheckedLaunchesTheEditorAndRecordsTheSample()
  {
    var model = NewCopyMain(new MemoryTreeFileSystem());
    model.Samples.OpenCopyCommand.Execute(model.Samples.Samples.Single(item => item.Name == "Coin Rush"));
    Assert.True(model.NewProject!.OpenInEditor);

    model.NewProject.CreateCommand.Execute(null);

    var copy = Path.Combine(LauncherRig.DefaultProjects, "Coin Rush");
    var launch = Assert.Single(_runner.Launches);
    Assert.EndsWith("ECS3DEditor", launch.FileName);
    Assert.Equal(new[] { "--project", copy }, launch.Arguments.ToArray());
    var entry = Assert.Single(
      (JsonArray)JsonNode.Parse(_rig.Files.TextOf(_rig.RecentsPath)!)!["projects"]!);
    Assert.Equal("rush", (string)entry!["template"]!);
  }

  [Fact]
  public void CreatingACopyWritesToTheChosenLocationAndRecordsTheSample()
  {
    var files = new MemoryTreeFileSystem();
    var model = NewCopyMain(files);
    var shipped = FolderOf(LauncherRig.LauncherDir, "rush");
    var before = files.EnumerateFiles(shipped).OrderBy(path => path).ToArray();
    Assert.NotEmpty(before);
    model.Samples.OpenCopyCommand.Execute(model.Samples.Samples.Single(item => item.Name == "Coin Rush"));
    model.NewProject!.OpenInEditor = false;

    model.NewProject.CreateCommand.Execute(null);

    var copy = Path.Combine(LauncherRig.DefaultProjects, "Coin Rush");
    Assert.Null(model.NewProject);
    Assert.True(files.FileExists(Path.Combine(copy, "project.ecs3d")));
    Assert.Equal(before, files.EnumerateFiles(shipped).OrderBy(path => path).ToArray());
    var entry = Assert.Single(
      (JsonArray)JsonNode.Parse(_rig.Files.TextOf(_rig.RecentsPath)!)!["projects"]!);
    Assert.Equal(copy, (string)entry!["path"]!);
    Assert.Equal("rush", (string)entry["template"]!);
  }
}
