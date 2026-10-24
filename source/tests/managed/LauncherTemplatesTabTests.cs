using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Avalonia.Media.Imaging;
using ECS3DLauncher.Services;
using ECS3DLauncher.Shell;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherTemplatesTabTests
{
  private readonly LauncherRig _rig = new();
  private readonly FakeProcessRunner _runner = new();

  private static string TemplatesOf(string engineFolder) => Path.Combine(Path.GetFullPath(engineFolder), "templates");

  private static string FolderOf(string engineFolder, string id) => Path.Combine(TemplatesOf(engineFolder), id);

  private static string Manifest(
    string id, string name, string category, string description, string tags, string minEngine, string extra) =>
    "{\"id\":\"" + id + "\",\"name\":\"" + name + "\",\"description\":\"" + description + "\",\"category\":\""
    + category + "\",\"tags\":[" + tags + "],\"thumbnail\":\"thumbnail.png\",\"entryScene\":\"Main\","
    + "\"minEngineVersion\":\"" + minEngine + "\"" + extra + "}";

  private void AddEntry(string engineFolder, string id, string json)
  {
    _rig.Files.AddDirectory(TemplatesOf(engineFolder));
    _rig.Files.AddDirectory(FolderOf(engineFolder, id));
    _rig.Files.Seed(Path.Combine(FolderOf(engineFolder, id), "template.json"), json);
  }

  private void AddTemplate(
    string engineFolder, string id, string name, string description, string tags,
    string minEngine = "0.0.1", string extra = "") =>
    AddEntry(engineFolder, id, Manifest(id, name, "template", description, tags, minEngine, extra));

  private void AddSample(string engineFolder, string id, string name) =>
    AddEntry(engineFolder, id, Manifest(id, name, "sample", "A sample", "\"demo\"", "0.0.1", ""));

  private void AddStandardEntries(string engineFolder)
  {
    AddTemplate(engineFolder, "arena", "Arena", "A small battle field", "\"Arcade\",\"Co-op\"", extra: ",\"players\":{\"min\":1,\"max\":4}");
    AddTemplate(engineFolder, "blank", "Blank", "Start empty", "\"empty\"");
    AddSample(engineFolder, "rush", "Coin Rush");
  }

  private MainWindowViewModel NewMain(
    Func<string, Bitmap?>? loadThumbnail = null, string version = "1.2.3", TemplateCatalog? catalog = null) =>
    new(
      _rig.NewEditor(),
      _rig.NewRecents(),
      _runner,
      new EarlyExitWatcher(ShellFixtures.UserData, TimeSpan.FromHours(1)),
      catalog ?? _rig.NewCatalog(),
      loadThumbnail ?? (_ => null),
      version,
      action => action(),
      () => _rig.Time.Now);

  private static string[] Names(MainWindowViewModel model) =>
    model.Templates.VisibleTemplates.Select(card => card.Name).ToArray();

  [Fact]
  public void OnlyTemplatesBecomeCardsInCatalogOrder()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddStandardEntries(LauncherRig.LauncherDir);

    var model = NewMain();

    Assert.Equal(new[] { "Arena", "Blank" }, model.Templates.Templates.Select(card => card.Name).ToArray());
    Assert.DoesNotContain(model.Templates.Templates, card => card.Name == "Coin Rush");
    Assert.Equal(new[] { "Coin Rush" }, model.Samples.Samples.Select(card => card.Name).ToArray());
    Assert.False(model.Templates.IsEmpty);
  }

  [Fact]
  public void CardFieldsComeFromTheManifest()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddStandardEntries(LauncherRig.LauncherDir);

    var cards = NewMain().Templates.Templates;
    var arena = cards.Single(card => card.Name == "Arena");
    var blank = cards.Single(card => card.Name == "Blank");

    Assert.Equal("arena", arena.Id);
    Assert.Equal("A small battle field", arena.Description);
    Assert.Equal(new[] { "Arcade", "Co-op" }, arena.Tags.ToArray());
    Assert.Equal(FolderOf(LauncherRig.LauncherDir, "arena"), arena.FolderPath);
    Assert.Equal("1-4 players", arena.PlayersLabel);
    Assert.True(arena.HasPlayers);
    Assert.Equal("blank", blank.Id);
    Assert.Null(blank.PlayersLabel);
    Assert.False(blank.HasPlayers);
  }

  [Theory]
  [InlineData("1.2.3", "1.2.3", false)]
  [InlineData("1.2.3", "1.2.4", true)]
  [InlineData("1.2.3", "2.0", true)]
  [InlineData("1.2.3", "dev", true)]
  [InlineData("1.2.3", "1.0", false)]
  [InlineData("dev", "1.2.4", false)]
  [InlineData("dev", "dev", false)]
  [InlineData("not-a-version-", "9.0", false)]
  public void ATemplateNeedingANewerEngineIsGated(string launcher, string minEngine, bool gated)
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddTemplate(LauncherRig.LauncherDir, "only", "Only", "d", "\"a\"", minEngine);

    var card = Assert.Single(NewMain(version: launcher).Templates.Templates);

    Assert.Equal(gated, card.NeedsNewerEngine);
    Assert.Equal(!gated, card.IsAvailable);
  }

  [Fact]
  public void GatingIsPerCard()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddTemplate(LauncherRig.LauncherDir, "old", "Old", "d", "\"a\"", "1.0");
    AddTemplate(LauncherRig.LauncherDir, "new", "New", "d", "\"a\"", "5.0");

    var cards = NewMain().Templates.Templates;

    Assert.True(cards.Single(card => card.Name == "Old").IsAvailable);
    Assert.True(cards.Single(card => card.Name == "New").NeedsNewerEngine);
  }

  [Theory]
  [InlineData("battle", "Arena")]
  [InlineData("START EMPTY", "Blank")]
  [InlineData("co-op", "Arena")]
  [InlineData("EMPTY", "Blank")]
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
    Assert.False(model.Templates.HasNoMatches);

    model.SearchText = "zzz-nothing";

    Assert.Empty(Names(model));
    Assert.True(model.Templates.HasNoMatches);
    Assert.True(model.Templates.ShowEmptyState);
    Assert.Equal("No templates match your search", model.Templates.EmptyHeading);

    model.SearchText = "";

    Assert.Equal(2, Names(model).Length);
    Assert.False(model.Templates.ShowEmptyState);
  }

  [Fact]
  public void NoTemplatesExplainWhyTheTabIsEmpty()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);

    var missing = NewMain().Templates;

    Assert.True(missing.IsEmpty);
    Assert.True(missing.ShowEmptyState);
    Assert.False(missing.HasNoMatches);
    Assert.Equal($"No templates folder found at {TemplatesOf(LauncherRig.LauncherDir)}", missing.EmptyHeading);

    AddSample(LauncherRig.LauncherDir, "rush", "Coin Rush");

    var empty = NewMain().Templates;

    Assert.True(empty.IsEmpty);
    Assert.Equal($"No templates found in {TemplatesOf(LauncherRig.LauncherDir)}", empty.EmptyHeading);
  }

  [Fact]
  public void AnUnreadableTemplatesFolderIsNamed()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddStandardEntries(LauncherRig.LauncherDir);
    var readable = NewMain();
    Assert.False(readable.Templates.IsEmpty);
    _rig.Files.FailEnumerate = true;

    var templates = NewMain().Templates;

    Assert.True(templates.IsEmpty);
    Assert.Equal($"Could not read {TemplatesOf(LauncherRig.LauncherDir)}", templates.EmptyHeading);
  }

  [Fact]
  public void TheTemplatesNavCountIsTheNumberOfTemplates()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddStandardEntries(LauncherRig.LauncherDir);

    var model = NewMain();

    Assert.Equal("2", model.NavItems.Single(item => item.Key == "templates").Count);
    Assert.Equal("1", model.NavItems.Single(item => item.Key == "samples").Count);
  }

  [Fact]
  public void SearchTextInTheShellFiltersTheTemplates()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddStandardEntries(LauncherRig.LauncherDir);
    var model = NewMain();

    model.SearchText = "arcade";

    Assert.Equal("arcade", model.Templates.SearchText);
    Assert.Equal(new[] { "Arena" }, Names(model));
  }

  [Fact]
  public void ChangingTheEngineFolderReloadsTheTemplatesAndTheNavCount()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddStandardEntries(LauncherRig.LauncherDir);
    _rig.AddEngine(LauncherRig.OtherDir);
    AddTemplate(LauncherRig.OtherDir, "only", "Only One", "The sole template", "\"x\"");
    var model = NewMain();
    Assert.Equal(2, model.Templates.Templates.Count);
    Assert.Equal("2", model.NavItems.Single(item => item.Key == "templates").Count);

    model.Settings.EngineFolder = LauncherRig.OtherDir;

    var card = Assert.Single(model.Templates.Templates);
    Assert.Equal("Only One", card.Name);
    Assert.Equal(FolderOf(LauncherRig.OtherDir, "only"), card.FolderPath);
    Assert.Equal("1", model.NavItems.Single(item => item.Key == "templates").Count);
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
    Assert.Equal(new[] { "Arena" }, Names(model));

    model.Settings.EngineFolder = LauncherRig.OtherDir;

    Assert.Equal(new[] { "Arena" }, Names(model));
    Assert.Equal(FolderOf(LauncherRig.OtherDir, "arena"), model.Templates.VisibleTemplates[0].FolderPath);
  }

  [Fact]
  public void TheCatalogIsReadOncePerLoad()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddStandardEntries(LauncherRig.LauncherDir);
    _rig.AddEngine(LauncherRig.OtherDir);
    AddTemplate(LauncherRig.OtherDir, "only", "Only One", "d", "\"x\"");
    var counting = new CountingFileSystem(_rig.Files);
    var model = NewMain(catalog: new TemplateCatalog(counting, _rig.Log));

    Assert.Equal(1, counting.Enumerations);
    Assert.Equal(2, model.Templates.Templates.Count);
    Assert.Single(model.Samples.Samples);

    model.Settings.EngineFolder = LauncherRig.OtherDir;

    Assert.Equal(2, counting.Enumerations);
    Assert.Single(model.Templates.Templates);
  }

  [Fact]
  public void OnlyATemplateWithAThumbnailFileCallsTheLoader()
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    AddStandardEntries(LauncherRig.LauncherDir);
    var thumbnail = Path.Combine(FolderOf(LauncherRig.LauncherDir, "arena"), "thumbnail.png");
    _rig.Files.Seed(thumbnail, "png");
    var requested = new List<string>();

    var cards = NewMain(path =>
    {
      requested.Add(path);
      return null;
    }).Templates.Templates;

    Assert.Equal(new[] { thumbnail }, requested.ToArray());
    Assert.False(cards.Single(card => card.Name == "Arena").HasThumbnail);
    Assert.False(cards.Single(card => card.Name == "Blank").HasThumbnail);
  }

  private sealed class CountingFileSystem(IFileSystem inner) : IFileSystem
  {
    public int Enumerations { get; private set; }

    public bool FileExists(string path) => inner.FileExists(path);

    public bool DirectoryExists(string path) => inner.DirectoryExists(path);

    public IReadOnlyList<string> EnumerateDirectories(string path)
    {
      Enumerations++;
      return inner.EnumerateDirectories(path);
    }

    public string ReadAllText(string path) => inner.ReadAllText(path);

    public void WriteAllTextAtomic(string path, string contents) => inner.WriteAllTextAtomic(path, contents);

    public void CreateDirectory(string path) => inner.CreateDirectory(path);
  }
}
