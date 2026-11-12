using System;
using System.IO;
using System.Linq;
using System.Text.Json.Nodes;
using ECS3DLauncher.Services;
using ECS3DLauncher.Shell;
using ECS3DLauncher.Tabs.Library;
using Xunit;

namespace ECS3DManagedTests;

// The Library tab replaced the Templates and Samples tabs: one page, one search, and a Welcome scene entry.
public class LauncherLibraryTabTests
{
  private readonly LauncherRig _rig = new();
  private readonly FakeProcessRunner _runner = new();

  private static string TemplatesOf(string engineFolder) => Path.Combine(Path.GetFullPath(engineFolder), "templates");

  private void AddEntry(string id, string name, string category, string description, string tags)
  {
    var folder = Path.Combine(TemplatesOf(LauncherRig.LauncherDir), id);
    _rig.Files.AddDirectory(TemplatesOf(LauncherRig.LauncherDir));
    _rig.Files.AddDirectory(folder);
    _rig.Files.Seed(
      Path.Combine(folder, "template.json"),
      "{\"id\":\"" + id + "\",\"name\":\"" + name + "\",\"description\":\"" + description + "\",\"category\":\""
      + category + "\",\"tags\":[" + tags + "],\"thumbnail\":\"thumbnail.png\",\"entryScene\":\"Main\","
      + "\"minEngineVersion\":\"0.0.1\"}");
  }

  // Two templates (Arena, Blank) and two samples (Coin Rush, Physics Playground), each searchable by its own word.
  private void AddCatalog()
  {
    AddEntry("arena", "Arena", "template", "A small battle field", "\"arcade\"");
    AddEntry("blank", "Blank", "template", "Start empty", "\"starter\"");
    AddEntry("rush", "Coin Rush", "sample", "Grab every coin", "\"racing\"");
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

  private static string[] Sections(LibraryViewModel library) =>
    new[]
    {
      library.ShowTemplatesSection ? "templates" : null,
      library.ShowExamplesSection ? "examples" : null,
      library.ShowNoResults ? "none" : null,
    }.OfType<string>().ToArray();

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
  }

  [Fact]
  public void TheWelcomeSceneIsNotCountedInTheLibraryCount()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);

    var model = NewMain();

    Assert.Equal("0", model.NavItems.Single(item => item.Key == "library").Count);
    Assert.True(model.Library.ShowWelcome);
  }

  [Fact]
  public void OneSearchFiltersTheTemplatesTheSamplesAndTheWelcomeEntryTogether()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddCatalog();
    var model = NewMain();
    Assert.Equal(2, model.Library.Templates.VisibleTemplates.Count);
    Assert.Equal(2, model.Library.Samples.VisibleSamples.Count);
    Assert.True(model.Library.ShowWelcome);

    model.SearchText = "around";

    Assert.Equal("around", model.Library.SearchText);
    Assert.Equal("around", model.Library.Templates.SearchText);
    Assert.Equal("around", model.Library.Samples.SearchText);
    Assert.Empty(model.Library.Templates.VisibleTemplates);
    Assert.Equal(
      new[] { "Physics Playground" },
      model.Library.Samples.VisibleSamples.Select(card => card.Name).ToArray());
    Assert.True(model.Library.ShowWelcome);

    model.SearchText = "arcade";

    Assert.Equal(new[] { "Arena" }, model.Library.Templates.VisibleTemplates.Select(card => card.Name).ToArray());
    Assert.Empty(model.Library.Samples.VisibleSamples);
    Assert.False(model.Library.ShowWelcome);
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
    Assert.Equal(2, model.Library.Templates.VisibleTemplates.Count);

    model.SearchText = query;

    Assert.True(model.Library.ShowWelcome);
    Assert.Empty(model.Library.Templates.VisibleTemplates);
    Assert.Empty(model.Library.Samples.VisibleSamples);
    Assert.Equal(new[] { "examples" }, Sections(model.Library));
  }

  [Fact]
  public void TheWelcomeEntryIsPresentWithAnEmptyCatalog()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);

    var library = NewMain().Library;

    Assert.True(library.Templates.IsEmpty);
    Assert.True(library.Samples.IsEmpty);
    Assert.True(library.ShowWelcome);
    Assert.Equal(new[] { "templates", "examples" }, Sections(library));
    Assert.Equal($"No templates folder found at {TemplatesOf(LauncherRig.LauncherDir)}", library.Templates.EmptyHeading);
  }

  [Fact]
  public void TheWelcomeEntryStaysOpenableOnceAProjectExists()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    SeedRecentProject("/work/alpha");

    var model = NewMain();

    Assert.False(model.Projects.IsEmpty);
    Assert.True(model.Library.ShowWelcome);
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
    Assert.True(model.Library.ShowWelcome);
  }

  [Fact]
  public void ASectionHidesWhenNoneOfItsItemsMatch()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddCatalog();
    var library = NewMain().Library;
    Assert.Equal(new[] { "templates", "examples" }, Sections(library));

    library.SearchText = "arcade";

    Assert.Equal(new[] { "templates" }, Sections(library));
    Assert.False(library.ShowWelcome);

    library.SearchText = "racing";

    Assert.Equal(new[] { "examples" }, Sections(library));
    Assert.False(library.ShowWelcome);

    library.SearchText = "";

    Assert.Equal(new[] { "templates", "examples" }, Sections(library));
  }

  [Fact]
  public void NoMatchAnywhereShowsOneEmptyStateAndHidesEverySection()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddCatalog();
    var library = NewMain().Library;
    Assert.False(library.ShowNoResults);

    library.SearchText = "zzz-nothing";

    Assert.Equal(new[] { "none" }, Sections(library));
    Assert.False(library.ShowWelcome);
    Assert.Equal("No results for your search", library.NoResultsHeading);

    library.SearchText = "";

    Assert.False(library.ShowNoResults);
  }

  [Fact]
  public void AnEmptyCatalogKeepsItsExplanationUntilASearchNarrowsThePage()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    var library = NewMain().Library;
    Assert.True(library.ShowTemplatesSection);

    library.SearchText = "welcome";

    Assert.Equal(new[] { "examples" }, Sections(library));

    library.SearchText = "zzz-nothing";

    Assert.Equal(new[] { "none" }, Sections(library));
  }

  [Fact]
  public void ChangingTheEngineFolderReloadsBothChildren()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddCatalog();
    _rig.AddEngine(LauncherRig.OtherDir);
    var model = NewMain();
    model.SearchText = "arcade";
    Assert.Single(model.Library.Templates.VisibleTemplates);

    model.Settings.EngineFolder = LauncherRig.OtherDir;

    Assert.Empty(model.Library.Templates.Templates);
    Assert.Empty(model.Library.Samples.Samples);
    Assert.Equal("0", model.NavItems.Single(item => item.Key == "library").Count);
    Assert.Equal(new[] { "none" }, Sections(model.Library));
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
