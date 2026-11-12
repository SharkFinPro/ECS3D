using System;
using System.IO;
using System.Linq;
using System.Text.Json.Nodes;
using ECS3DLauncher.Services;
using ECS3DLauncher.Shared.Models;
using ECS3DLauncher.Shell;
using ECS3DLauncher.Tabs.Library;
using Xunit;

namespace ECS3DManagedTests;

// The Library tab replaced the Templates and Samples tabs: one flat list, one search, and a Welcome scene entry.
public class LauncherLibraryTabTests
{
  private readonly LauncherRig _rig = new();
  private readonly FakeProcessRunner _runner = new();

  private static string TemplatesOf(string engineFolder) => Path.Combine(Path.GetFullPath(engineFolder), "templates");

  private void AddEntry(string id, string name, string category, string description, string tags, string extra = "")
  {
    var folder = Path.Combine(TemplatesOf(LauncherRig.LauncherDir), id);
    _rig.Files.AddDirectory(TemplatesOf(LauncherRig.LauncherDir));
    _rig.Files.AddDirectory(folder);
    _rig.Files.Seed(
      Path.Combine(folder, "template.json"),
      "{\"id\":\"" + id + "\",\"name\":\"" + name + "\",\"description\":\"" + description + "\",\"category\":\""
      + category + "\",\"tags\":[" + tags + "],\"thumbnail\":\"thumbnail.png\",\"entryScene\":\"Main\","
      + "\"minEngineVersion\":\"0.0.1\"" + extra + "}");
  }

  // Two templates (Arena, Blank) and two samples (Coin Rush, Physics Playground), each searchable by its own word.
  private void AddCatalog()
  {
    AddEntry("arena", "Arena", "template", "A small battle field", "\"arcade\"");
    AddEntry("blank", "Blank", "template", "Start empty", "\"starter\"");
    AddEntry("rush", "Coin Rush", "sample", "Grab every coin", "\"racing\"", ",\"players\":{\"min\":1,\"max\":4}");
    AddEntry("playground", "Physics Playground", "sample", "Push boxes around", "\"physics\"");
  }

  private void SeedRecentProject(string path)
  {
    var entry = new JsonObject { ["path"] = path, ["name"] = "alpha", ["lastOpenedUtc"] = "2026-10-18T10:00:00Z", ["pinned"] = false };
    var document = new JsonObject { ["version"] = 1, ["projects"] = new JsonArray(entry) };
    _rig.Files.Seed(_rig.RecentsPath, document.ToJsonString());
  }

  private MainWindowViewModel NewMain() =>
    new(
      _rig.NewEditor(),
      _rig.NewRecents(),
      _runner,
      new EarlyExitWatcher(ShellFixtures.UserData, TimeSpan.FromHours(1)),
      _rig.NewCatalog(),
      _ => null,
      "1.2.3",
      action => action(),
      () => _rig.Time.Now) { ToastDelay = ToastDelays.Never };

  private static LibraryItemKind[] Kinds(LibraryViewModel library) => library.Items.Select(item => item.Kind).ToArray();

  private static string[] Names(LibraryViewModel library) => library.Items.Select(item => item.Name).ToArray();

  private static bool HasWelcome(LibraryViewModel library) => Kinds(library).Contains(LibraryItemKind.Tour);

  [Theory]
  [InlineData("library")]
  [InlineData("templates")]
  [InlineData("samples")]
  public void LibraryOwnsItsKeyAndTheOldTabKeys(string key)
  {
    Assert.Equal(LauncherTab.Library, LauncherTabs.Parse(key));
    Assert.Equal("library", LauncherTabs.Key(LauncherTab.Library));
    Assert.Equal(LauncherTab.Projects, LauncherTabs.Parse("nonsense"));
  }

  [Fact]
  public void TheLibraryHeaderDescribesAllThreeKindsOfEntry()
  {
    var header = LauncherTabs.Header(LauncherTab.Library);

    Assert.Equal("Library", header.Title);
    Assert.Equal("Start from a template, run a finished sample, or take the Welcome tour.", header.Subtitle);
    Assert.Equal("Search the library", header.SearchPlaceholder);
    Assert.True(header.ShowSearch);
    Assert.False(header.ShowViewToggle);
  }

  [Fact]
  public void TheSidebarListsProjectsLibraryAndLearnWithALibraryCountOfTemplatesPlusSamples()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddCatalog();

    var model = NewMain();

    Assert.Equal(new[] { "projects", "library", "learn" }, model.NavItems.Select(item => item.Key).ToArray());
    Assert.Equal(new[] { "Projects", "Library", "Learn" }, model.NavItems.Select(item => item.Label).ToArray());
    Assert.Equal(2, model.Library.Templates.Templates.Count);
    Assert.Equal(2, model.Library.Samples.Samples.Count);
    Assert.Equal("4", model.NavItems.Single(item => item.Key == "library").Count);
    Assert.Equal(5, model.Library.Items.Count);
  }

  [Fact]
  public void TheWelcomeSceneIsNotCountedInTheLibraryCount()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);

    var model = NewMain();

    Assert.Equal("0", model.NavItems.Single(item => item.Key == "library").Count);
    Assert.True(HasWelcome(model.Library));
  }

  [Fact]
  public void TheListIsOneFlatSequenceOfWelcomeThenTemplatesThenSamples()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddCatalog();

    var library = NewMain().Library;

    Assert.Equal(
      new[]
      {
        LibraryItemKind.Tour, LibraryItemKind.Template, LibraryItemKind.Template, LibraryItemKind.Sample,
        LibraryItemKind.Sample,
      },
      Kinds(library));
    Assert.Equal(
      new[] { "Welcome scene", "Arena", "Blank", "Coin Rush", "Physics Playground" },
      Names(library));
  }

  [Fact]
  public void EveryRowCarriesAKindPill()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddCatalog();

    var items = NewMain().Library.Items;

    Assert.Equal(
      new[] { "Tour", "Template", "Template", "Sample", "Sample" },
      items.Select(item => item.KindLabel).ToArray());
    Assert.All(items, item => Assert.False(string.IsNullOrEmpty(item.KindLabel)));
  }

  [Fact]
  public void EachKindIsWiredToItsOwnCard()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddCatalog();
    var library = NewMain().Library;

    var tour = library.Items.Single(item => item.IsTour);
    var template = library.Items.First(item => item.IsTemplate);
    var sample = library.Items.Single(item => item.Name == "Coin Rush");
    var solo = library.Items.Single(item => item.Name == "Physics Playground");

    Assert.Null(tour.Template);
    Assert.Null(tour.Sample);
    Assert.Same(library.Templates.Templates.First(), template.Template);
    Assert.Null(template.Sample);
    Assert.Same(library.Samples.Samples.First(card => card.Name == "Coin Rush"), sample.Sample);
    Assert.Null(sample.Template);
    Assert.True(sample.IsMultiplayerSample);
    Assert.False(solo.IsMultiplayerSample);
    Assert.False(template.IsMultiplayerSample);
    Assert.Equal("1-4 players", sample.PlayersLabel);
    Assert.True(sample.HasPlayers);
    Assert.False(template.HasPlayers);
  }

  [Fact]
  public void ATemplateRowOpensTheNewProjectDialogForItsTemplate()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddCatalog();
    var model = NewMain();
    var template = model.Library.Items.Single(item => item.Name == "Blank");
    Assert.Null(model.NewProject);

    Assert.True(model.Library.Templates.OpenCommand.CanExecute(template.Template));
    model.Library.Templates.OpenCommand.Execute(template.Template);

    Assert.Equal("Blank", model.NewProject!.TemplateName);
  }

  [Fact]
  public void ASampleRowPlaysAndOpensACopyOfItsSample()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddCatalog();
    var model = NewMain();
    var sample = model.Library.Items.Single(item => item.Name == "Coin Rush");

    Assert.Null(model.NewProject);
    Assert.Empty(_runner.Launches);

    model.Library.Samples.PlayCommand.Execute(sample.Sample);

    var launch = Assert.Single(_runner.Launches);
    Assert.Equal(Path.Combine(LauncherRig.LauncherDir, "ECS3DClient"), launch.FileName);
    Assert.Contains(sample.Sample!.FolderPath, launch.Arguments);
    Assert.Null(model.NewProject);

    model.Library.Samples.OpenCopyCommand.Execute(sample.Sample);

    Assert.NotNull(model.NewProject);
  }

  [Fact]
  public void OneSearchFiltersTheTemplatesTheSamplesAndTheWelcomeEntryTogether()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddCatalog();
    var model = NewMain();
    Assert.Equal(5, model.Library.Items.Count);

    model.SearchText = "around";

    Assert.Equal("around", model.Library.SearchText);
    Assert.Equal("around", model.Library.Templates.SearchText);
    Assert.Equal("around", model.Library.Samples.SearchText);
    Assert.Equal(new[] { "Welcome scene", "Physics Playground" }, Names(model.Library));

    model.SearchText = "arcade";

    Assert.Equal(new[] { "Arena" }, Names(model.Library));

    model.SearchText = "";

    Assert.Equal(5, model.Library.Items.Count);
  }

  [Theory]
  [InlineData("welcome")]
  [InlineData("TOUR")]
  [InlineData("explore")]
  [InlineData("built-in")]
  public void WelcomeFindsOnlyTheWelcomeEntry(string query)
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddCatalog();
    var model = NewMain();
    Assert.Equal(5, model.Library.Items.Count);

    model.SearchText = query;

    Assert.Equal(new[] { LibraryItemKind.Tour }, Kinds(model.Library));
    Assert.Empty(model.Library.Templates.VisibleTemplates);
    Assert.Empty(model.Library.Samples.VisibleSamples);
  }

  [Fact]
  public void TheWelcomeEntryIsPresentWithAnEmptyCatalogAndItsExplanationTakesTheTemplatesPlace()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);

    var library = NewMain().Library;

    Assert.True(library.Templates.IsEmpty);
    Assert.True(library.Samples.IsEmpty);
    Assert.Equal(new[] { LibraryItemKind.Tour, LibraryItemKind.Notice }, Kinds(library));
    var notice = library.Items[1];
    Assert.Equal($"No templates folder found at {TemplatesOf(LauncherRig.LauncherDir)}", notice.Name);
    Assert.True(notice.IsNotice);
    Assert.False(notice.IsRow);
    Assert.True(library.Items[0].IsRow);
  }

  [Fact]
  public void TheNoticeSitsBetweenTheWelcomeEntryAndTheSamplesWhenOnlyTemplatesAreMissing()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddEntry("rush", "Coin Rush", "sample", "Grab every coin", "\"racing\"");

    var library = NewMain().Library;

    Assert.Equal(
      new[] { LibraryItemKind.Tour, LibraryItemKind.Notice, LibraryItemKind.Sample }, Kinds(library));
    Assert.Equal($"No templates found in {TemplatesOf(LauncherRig.LauncherDir)}", library.Items[1].Name);
  }

  [Fact]
  public void TheWelcomeEntryStaysOpenableOnceAProjectExists()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    SeedRecentProject("/work/alpha");

    var model = NewMain();

    Assert.False(model.Projects.IsEmpty);
    Assert.True(HasWelcome(model.Library));
    Assert.Same(model.ExploreCommand, model.Library.ExploreCommand);
    Assert.True(model.Library.ExploreCommand.CanExecute(null));

    model.Library.ExploreCommand.Execute(null);

    var launch = Assert.Single(_runner.Launches);
    Assert.Equal(Path.Combine(LauncherRig.LauncherDir, "ECS3DEditor"), launch.FileName);
    Assert.Empty(launch.Arguments);
  }

  [Fact]
  public void TheWelcomeEntryFollowsTheEngineLikeTheOtherExploreButtons()
  {
    var model = NewMain();
    Assert.False(model.Library.ExploreCommand.CanExecute(null));
    Assert.NotNull(model.Library.ExploreTooltip);
    Assert.Equal(model.Projects.ExploreTooltip, model.Library.ExploreTooltip);

    _rig.AddEngine(LauncherRig.OtherDir);
    model.Settings.EngineFolder = LauncherRig.OtherDir;

    Assert.True(model.Library.ExploreCommand.CanExecute(null));
    Assert.Null(model.Library.ExploreTooltip);
    Assert.True(HasWelcome(model.Library));
  }

  [Fact]
  public void ARowDisappearsWhenItDoesNotMatchAndReturnsWhenTheSearchClears()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddCatalog();
    var library = NewMain().Library;
    Assert.Contains("Arena", Names(library));

    library.SearchText = "racing";

    Assert.Equal(new[] { "Coin Rush" }, Names(library));
    Assert.DoesNotContain("Arena", Names(library));
    Assert.False(HasWelcome(library));

    library.SearchText = "";

    Assert.Equal(5, library.Items.Count);
    Assert.Contains("Arena", Names(library));
  }

  [Fact]
  public void NoMatchAnywhereShowsOneEmptyStateAndNoRows()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddCatalog();
    var library = NewMain().Library;
    Assert.False(library.ShowNoResults);

    library.SearchText = "zzz-nothing";

    Assert.Empty(library.Items);
    Assert.True(library.ShowNoResults);
    Assert.Equal("No results for your search", library.NoResultsHeading);

    library.SearchText = "";

    Assert.False(library.ShowNoResults);
  }

  [Fact]
  public void AnEmptyCatalogKeepsItsExplanationUntilASearchNarrowsTheList()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    var library = NewMain().Library;
    Assert.Equal(new[] { LibraryItemKind.Tour, LibraryItemKind.Notice }, Kinds(library));

    library.SearchText = "welcome";

    Assert.Equal(new[] { LibraryItemKind.Tour }, Kinds(library));

    library.SearchText = "zzz-nothing";

    Assert.Empty(library.Items);
    Assert.True(library.ShowNoResults);
  }

  [Fact]
  public void ChangingTheEngineFolderReloadsBothChildren()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddCatalog();
    _rig.AddEngine(LauncherRig.OtherDir);
    var model = NewMain();
    model.SearchText = "arcade";
    Assert.Equal(new[] { "Arena" }, Names(model.Library));

    model.Settings.EngineFolder = LauncherRig.OtherDir;

    Assert.Empty(model.Library.Templates.Templates);
    Assert.Empty(model.Library.Samples.Samples);
    Assert.Equal("0", model.NavItems.Single(item => item.Key == "library").Count);
    Assert.Empty(model.Library.Items);
  }

  [Fact]
  public void TheProjectsAndLearnCallbacksOpenTheLibraryTab()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    var model = NewMain();
    Assert.Equal("projects", model.ActiveNav);

    model.Projects.BrowseSamplesCommand.Execute(null);

    Assert.Equal("library", model.ActiveNav);
    Assert.Same(model.Library, model.CurrentTab);

    model.SelectNavCommand.Execute("learn");
    Assert.Equal("learn", model.ActiveNav);
    model.Learn.GoToSamplesCommand.Execute(null);

    Assert.Equal("library", model.ActiveNav);
    Assert.Same(model.Library, model.CurrentTab);
  }
}
