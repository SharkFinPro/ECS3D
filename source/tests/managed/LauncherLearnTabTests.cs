using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using ECS3DLauncher.Services;
using ECS3DLauncher.Shell;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherLearnTabTests
{
  private readonly LauncherRig _rig = new();
  private readonly FakeProcessRunner _runner = new();

  private static string SourceRoot() =>
    typeof(LauncherLearnTabTests).Assembly
      .GetCustomAttributes<AssemblyMetadataAttribute>()
      .Single(a => a.Key == "ECS3DSourceRoot").Value!;

  private MainWindowViewModel NewMain() =>
    new(
      _rig.NewEditor(),
      _runner,
      new EarlyExitWatcher(ShellFixtures.UserData, TimeSpan.FromHours(1)),
      "1.2.3",
      action => action());

  private static string[] Titles(MainWindowViewModel model) => model.Learn.Guides.Select(item => item.Title).ToArray();

  [Fact]
  public void TheCatalogHasSixNumberedGuidesWithGithubLinks()
  {
    var guides = LearnCatalog.Guides;

    Assert.Equal(new[] { 1, 2, 3, 4, 5, 6 }, guides.Select(item => item.Number).ToArray());
    foreach (var guide in guides)
    {
      Assert.False(string.IsNullOrWhiteSpace(guide.Title));
      Assert.False(string.IsNullOrWhiteSpace(guide.Summary));
      Assert.True(Uri.TryCreate(guide.Url, UriKind.Absolute, out var uri));
      Assert.Equal("https", uri!.Scheme);
      Assert.StartsWith("https://github.com/SharkFinPro/ECS3D/blob/main/docs/guides/", guide.Url);
      Assert.EndsWith(".md", guide.Url);
    }

    Assert.Equal(guides.Count, guides.Select(item => item.Url).Distinct().Count());
  }

  [Fact]
  public void EveryLinkedGuideExistsInTheRepository()
  {
    var folder = Path.GetFullPath(Path.Combine(SourceRoot(), "..", "docs", "guides"));
    Assert.True(Directory.Exists(folder));

    foreach (var guide in LearnCatalog.Guides)
    {
      var file = Path.Combine(folder, guide.Url[LearnCatalog.GuidesUrl.Length..]);
      Assert.True(File.Exists(file), file);
    }
  }

  [Fact]
  public void TheBoxNumbersMatchTheShippedContent()
  {
    var root = SourceRoot();
    var categories = Directory.GetFiles(Path.Combine(root, "templates"), "template.json", SearchOption.AllDirectories)
      .Select(file =>
      {
        using var document = JsonDocument.Parse(File.ReadAllText(file));
        return document.RootElement.GetProperty("category").GetString();
      })
      .ToArray();
    var starters = Directory.GetFiles(Path.Combine(root, "libs", "scripting", "UserScripts", "Starters"), "*.cs")
      .Select(Path.GetFileName)
      .Where(name => name is not ("StarterEvents.cs" or "StarterUtils.cs"))
      .ToArray();
    var defaultProject = File.ReadAllText(Path.Combine(root, "apps", "server", "DefaultProject.cpp"));
    var shapesStart = defaultProject.IndexOf("libraryShapes()", StringComparison.Ordinal);
    var shapesText = defaultProject[shapesStart..defaultProject.IndexOf("return shapes;", shapesStart, StringComparison.Ordinal)];
    var shapeNames = Regex.Matches(shapesText, "\\{ \"([^\"]+)\", \\w+Hull \\}").Select(match => match.Groups[1].Value).ToArray();

    Assert.Equal(LearnCatalog.SampleCount, categories.Count(item => item == "sample"));
    Assert.Equal(LearnCatalog.TemplateCount, categories.Count(item => item == "template"));
    Assert.Equal(LearnCatalog.StarterScriptCount, starters.Length);
    Assert.Equal(LearnCatalog.ConvexShapeCount, shapeNames.Length);
    Assert.Equal(
      new[] { "D4", "D6", "D8", "D12", "D20", "Hex Prism", "Drum", "Gem", "Boulder" },
      shapeNames);
    Assert.Equal(
      "Every install includes 2 samples, 2 templates, 17 starter scripts and 9 convex shapes.",
      LearnCatalog.BoxSummary);
  }

  [Fact]
  public void SearchFiltersByTitleAndSummaryIgnoringCase()
  {
    var model = NewMain();
    Assert.Equal(6, model.Learn.Guides.Count);

    model.SearchText = "MULTIPLAYER";
    Assert.Equal(new[] { "Multiplayer" }, Titles(model));
    Assert.False(model.Learn.HasNoMatches);

    model.SearchText = "scorekeeper";
    Assert.Equal(new[] { "Scene Scripts and Events" }, Titles(model));

    model.SearchText = "   ";
    Assert.Equal(6, model.Learn.Guides.Count);
  }

  [Fact]
  public void ASearchWithNoMatchesShowsTheNoMatchesLine()
  {
    var model = NewMain();
    Assert.False(model.Learn.HasNoMatches);

    model.SearchText = "no such guide";
    Assert.Empty(model.Learn.Guides);
    Assert.True(model.Learn.HasNoMatches);

    model.SearchText = "";
    Assert.False(model.Learn.HasNoMatches);
  }

  [Fact]
  public void OpeningACardPassesItsUrlToTheOpener()
  {
    var model = NewMain();
    string? opened = null;
    model.OpenUrl = url =>
    {
      opened = url;
      return Task.FromResult(true);
    };
    var guide = LearnCatalog.Guides[2];

    model.Learn.OpenGuideCommand.Execute(guide);

    Assert.Equal(guide.Url, opened);
    Assert.False(model.HasToast);
  }

  [Theory]
  [InlineData("declined")]
  [InlineData("throws")]
  [InlineData("missing")]
  public void AFailedOpenShowsTheToast(string mode)
  {
    var model = NewMain();
    if (mode != "missing")
    {
      model.OpenUrl = _ => mode == "throws" ? throw new IOException("no browser") : Task.FromResult(false);
    }
    var guide = LearnCatalog.Guides[0];

    model.Learn.OpenGuideCommand.Execute(guide);

    Assert.Equal($"Could not open {guide.Url}", model.ToastMessage);
  }

  [Fact]
  public void GoToSamplesSwitchesTheActiveTab()
  {
    var model = NewMain();
    model.SelectNavCommand.Execute("learn");

    model.Learn.GoToSamplesCommand.Execute(null);

    Assert.Equal("samples", model.ActiveNav);
    Assert.Same(model.Samples, model.CurrentTab);
  }

  [Fact]
  public void OpenWelcomeSceneRunsTheShellExploreAndFollowsTheEngine()
  {
    var model = NewMain();
    Assert.False(model.Learn.ExploreCommand.CanExecute(null));
    Assert.NotNull(model.Learn.ExploreTooltip);

    _rig.AddEngine(LauncherRig.OtherDir);
    model.Settings.EngineFolder = LauncherRig.OtherDir;

    Assert.True(model.Learn.ExploreCommand.CanExecute(null));
    Assert.Null(model.Learn.ExploreTooltip);

    model.Learn.ExploreCommand.Execute(null);

    var launch = Assert.Single(_runner.Launches);
    Assert.Equal(Path.Combine(LauncherRig.OtherDir, "ECS3DEditor"), launch.FileName);
    Assert.Empty(launch.Arguments);
  }
}
