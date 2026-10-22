using System;
using System.IO;
using System.Linq;
using System.Reflection;
using ECS3DLauncher.Services;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherTemplateCatalogTests
{
  private static readonly string EngineRoot = Path.GetFullPath("engine");
  private static readonly string TemplatesRoot = Path.Combine(EngineRoot, "templates");

  private static string ManifestJson(string id, string name, string category) => ManifestJson(id, name, category, "");

  private static string ManifestJson(string id, string name, string category, string extra) =>
    "{\"id\":\"" + id + "\",\"name\":\"" + name + "\",\"description\":\"d\",\"category\":\"" + category
    + "\",\"tags\":[\"a\"],\"thumbnail\":\"thumbnail.png\",\"entryScene\":\"Main\",\"minEngineVersion\":\"0.0.1\""
    + extra + "}";

  private static string Folder(FakeFileSystem files, string folderName, string? manifest) =>
    Folder(files, folderName, manifest, false);

  private static string Folder(FakeFileSystem files, string folderName, string? manifest, bool thumbnail)
  {
    var folder = Path.Combine(TemplatesRoot, folderName);
    files.AddDirectory(TemplatesRoot);
    files.AddDirectory(folder);
    if (manifest is not null)
    {
      files.Seed(Path.Combine(folder, "template.json"), manifest);
    }

    if (thumbnail)
    {
      files.Seed(Path.Combine(folder, "thumbnail.png"), "png");
    }

    return folder;
  }

  private static TemplateCatalogContents Load(FakeFileSystem files, RecordingLauncherLog log) =>
    new TemplateCatalog(files, log).Load(EngineRoot);

  [Fact]
  public void GoodFoldersLoadAndSplitByCategory()
  {
    var files = new FakeFileSystem();
    Folder(files, "b", ManifestJson("zeta", "Zeta", "template"));
    Folder(files, "a", ManifestJson("alpha", "alpha", "template"));
    Folder(files, "c", ManifestJson("demo", "Demo", "sample"));
    var log = new RecordingLauncherLog();

    var contents = Load(files, log);

    Assert.True(contents.FolderFound);
    Assert.False(contents.IsEmpty);
    Assert.Equal(new[] { "alpha", "zeta" }, contents.Templates.Select(e => e.Manifest.Id).ToArray());
    Assert.Equal(new[] { "demo" }, contents.Samples.Select(e => e.Manifest.Id).ToArray());
    Assert.Equal(Path.Combine(TemplatesRoot, "c"), contents.Samples[0].FolderPath);
    Assert.Empty(log.Warnings);
  }

  [Fact]
  public void EqualNamesSortById()
  {
    var files = new FakeFileSystem();
    Folder(files, "a", ManifestJson("two", "Same", "template"));
    Folder(files, "b", ManifestJson("one", "Same", "template"));

    Assert.Equal(new[] { "one", "two" }, Load(files, new RecordingLauncherLog()).Templates.Select(e => e.Manifest.Id).ToArray());
  }

  [Theory]
  [InlineData("{ not json", "not valid JSON")]
  [InlineData("[]", "must be a JSON object")]
  [InlineData("{\"id\":\"x\"}", "missing required field 'name'")]
  [InlineData("{\"id\":5}", "field 'id' must be a string")]
  public void BadManifestsAreSkippedWithTheReason(string manifest, string reason)
  {
    var files = new FakeFileSystem();
    var good = Folder(files, "good", ManifestJson("good", "Good", "template"));
    var bad = Folder(files, "bad", manifest);
    var log = new RecordingLauncherLog();

    var contents = Load(files, log);

    Assert.Equal(new[] { "good" }, contents.Templates.Select(e => e.Manifest.Id).ToArray());
    var warning = Assert.Single(log.Warnings);
    Assert.Contains(bad, warning);
    Assert.Contains(reason, warning);
    Assert.DoesNotContain(good, warning);
  }

  [Fact]
  public void AFolderWithoutAManifestIsSkipped()
  {
    var files = new FakeFileSystem();
    var empty = Folder(files, "empty", null);
    Folder(files, "good", ManifestJson("good", "Good", "sample"));
    var log = new RecordingLauncherLog();

    var contents = Load(files, log);

    Assert.Single(contents.Samples);
    var warning = Assert.Single(log.Warnings);
    Assert.Contains(empty, warning);
    Assert.Contains("template.json not found", warning);
  }

  [Fact]
  public void AnUnreadableManifestIsSkipped()
  {
    var files = new FakeFileSystem();
    Folder(files, "locked", ManifestJson("locked", "Locked", "template"));
    files.FailReads = true;
    var log = new RecordingLauncherLog();

    Assert.True(Load(files, log).IsEmpty);
    Assert.Contains("could not be read", Assert.Single(log.Warnings));
  }

  [Theory]
  [InlineData("category\":\"template", "category\":\"game", "field 'category'")]
  [InlineData("\"id\":\"x\"", "\"id\":\"Bad_Id\"", "field 'id'")]
  [InlineData("\"name\":\"N\"", "\"name\":\"\"", "field 'name' must not be empty")]
  [InlineData("\"minEngineVersion\":\"0.0.1\"", "\"minEngineVersion\":\"soon\"", "'minEngineVersion' is not a version")]
  [InlineData("\"tags\":[\"a\"]", "\"tags\":[1]", "field 'tags'")]
  [InlineData("\"tags\":[\"a\"]", "\"tags\":\"a\"", "field 'tags'")]
  [InlineData("\"entryScene\":\"Main\"", "\"entryScene\":\"Main\",\"players\":{\"min\":3,\"max\":2}", "field 'players'")]
  [InlineData("\"entryScene\":\"Main\"", "\"entryScene\":\"Main\",\"players\":{\"min\":1}", "field 'players'")]
  public void InvalidFieldsAreSkipped(string find, string replace, string reason)
  {
    const string valid =
      "{\"id\":\"x\",\"name\":\"N\",\"description\":\"d\",\"category\":\"template\",\"tags\":[\"a\"],"
      + "\"thumbnail\":\"t.png\",\"entryScene\":\"Main\",\"minEngineVersion\":\"0.0.1\"}";
    var files = new FakeFileSystem();
    Folder(files, "positive", valid);
    var log = new RecordingLauncherLog();
    Assert.Single(Load(files, log).Templates);
    Assert.Empty(log.Warnings);

    var broken = new FakeFileSystem();
    Folder(broken, "negative", valid.Replace(find, replace));
    var brokenLog = new RecordingLauncherLog();

    Assert.True(Load(broken, brokenLog).IsEmpty);
    Assert.Contains(reason, Assert.Single(brokenLog.Warnings));
  }

  [Fact]
  public void ThumbnailPathIsFullWhenPresentAndNullWhenMissing()
  {
    var files = new FakeFileSystem();
    var withImage = Folder(files, "with", ManifestJson("with", "With", "template"), true);
    Folder(files, "without", ManifestJson("without", "Without", "template"));

    var contents = Load(files, new RecordingLauncherLog());

    Assert.Equal(Path.Combine(withImage, "thumbnail.png"), contents.Templates.Single(e => e.Manifest.Id == "with").ThumbnailPath);
    Assert.Null(contents.Templates.Single(e => e.Manifest.Id == "without").ThumbnailPath);
  }

  [Fact]
  public void AThumbnailOutsideTheFolderIsIgnored()
  {
    var files = new FakeFileSystem();
    var folder = Folder(files, "t", ManifestJson("t", "T", "template").Replace("thumbnail.png", "../x.png"));
    files.Seed(Path.Combine(folder, "../x.png"), "png");

    Assert.Null(Load(files, new RecordingLauncherLog()).Templates[0].ThumbnailPath);
  }

  [Fact]
  public void MissingAndEmptyTemplatesFoldersAreTold()
  {
    var missing = Load(new FakeFileSystem(), new RecordingLauncherLog());
    Assert.False(missing.FolderFound);
    Assert.True(missing.IsEmpty);
    Assert.Equal(TemplatesRoot, missing.TemplatesFolder);

    var files = new FakeFileSystem();
    files.AddDirectory(TemplatesRoot);
    var empty = Load(files, new RecordingLauncherLog());
    Assert.True(empty.FolderFound);
    Assert.True(empty.IsEmpty);
  }

  [Fact]
  public void PlayersDecideMultiplayerAndTheLabel()
  {
    var files = new FakeFileSystem();
    Folder(files, "solo", ManifestJson("solo", "Solo", "template"));
    Folder(files, "one", ManifestJson("one", "One", "template", ",\"players\":{\"min\":1,\"max\":1}"));
    Folder(files, "party", ManifestJson("party", "Party", "template", ",\"players\":{\"min\":1,\"max\":4}"));

    var byId = Load(files, new RecordingLauncherLog()).Templates.ToDictionary(e => e.Manifest.Id, e => e.Manifest);

    Assert.False(byId["solo"].IsMultiplayer);
    Assert.Null(byId["solo"].PlayersLabel);
    Assert.False(byId["one"].IsMultiplayer);
    Assert.Null(byId["one"].PlayersLabel);
    Assert.True(byId["party"].IsMultiplayer);
    Assert.Equal("1-4 players", byId["party"].PlayersLabel);
  }

  [Theory]
  [InlineData("0.0.1", "0.0.1", false)]
  [InlineData("0.0.2", "0.0.1", true)]
  [InlineData("0.1", "0.0.9", true)]
  [InlineData("1", "1.0.0", false)]
  [InlineData("1.0.0", "1", false)]
  [InlineData("1.0.1", "1", true)]
  [InlineData("0.9.9", "0.10.0", false)]
  [InlineData("1.0.0", "1.0.0-beta", true)]
  [InlineData("1.0.0-beta", "1.0.0", false)]
  [InlineData("1.0.0-beta", "1.0.0-beta", false)]
  [InlineData("1.0.0-beta", "1.0.0-alpha", true)]
  [InlineData("0.0.1", "0.0.1+abc123", false)]
  [InlineData("99.0.0", "dev", false)]
  [InlineData("0.0.1", "dev", false)]
  [InlineData("0.0.1", "garbled", false)]
  [InlineData("garbled", "0.0.1", false)]
  public void MinEngineVersionGating(string required, string launcher, bool needsNewer)
  {
    Assert.Equal(needsNewer, TemplateCompatibility.NeedsNewerEngine(required, launcher));
  }

  [Fact]
  public void DevIsNewerThanEveryRelease()
  {
    Assert.True(EngineVersion.TryParse("dev", out var dev));
    Assert.True(EngineVersion.TryParse("999.0.0", out var release));
    Assert.True(EngineVersion.Compare(dev, release) > 0);
    Assert.True(EngineVersion.Compare(release, dev) < 0);
    Assert.Equal(0, EngineVersion.Compare(dev, EngineVersion.Dev));
    Assert.False(TemplateCompatibility.NeedsNewerEngine("dev", "dev"));
    Assert.True(TemplateCompatibility.NeedsNewerEngine("dev", "99.0.0"));
  }

  [Theory]
  [InlineData("")]
  [InlineData("1..2")]
  [InlineData("1.x")]
  [InlineData("-beta")]
  [InlineData("1.0-")]
  [InlineData("-1.0")]
  public void NonVersionsDoNotParse(string text)
  {
    Assert.False(EngineVersion.TryParse(text, out _));
  }

  [Fact]
  public void ValidNamesPass()
  {
    Assert.Null(ProjectNameRules.Validate("My Project"));
    Assert.Null(ProjectNameRules.Validate("First-Person 2"));
    Assert.Null(ProjectNameRules.Validate(new string('a', 64)));
    Assert.Null(ProjectNameRules.Validate("Console"));
    Assert.Null(ProjectNameRules.Validate("COM10"));
    Assert.Null(ProjectNameRules.Validate("a.b"));
  }

  [Theory]
  [InlineData(null)]
  [InlineData("")]
  [InlineData("   ")]
  public void EmptyNamesFail(string? name)
  {
    Assert.Contains("Enter a project name", ProjectNameRules.Validate(name));
  }

  [Fact]
  public void TooLongNamesFail()
  {
    Assert.Contains("at most 64", ProjectNameRules.Validate(new string('a', 65)));
  }

  [Theory]
  [InlineData("a<b")]
  [InlineData("a>b")]
  [InlineData("a:b")]
  [InlineData("a\"b")]
  [InlineData("a/b")]
  [InlineData("a\\b")]
  [InlineData("a|b")]
  [InlineData("a?b")]
  [InlineData("a*b")]
  [InlineData("a\tb")]
  [InlineData("a\u0001b")]
  public void InvalidCharactersFail(string name)
  {
    Assert.Contains("cannot contain", ProjectNameRules.Validate(name));
  }

  [Theory]
  [InlineData(" a")]
  [InlineData("a ")]
  [InlineData(".a")]
  [InlineData("a.")]
  [InlineData("..")]
  public void LeadingAndTrailingSpacesAndDotsFail(string name)
  {
    Assert.Contains("start or end", ProjectNameRules.Validate(name));
  }

  [Theory]
  [InlineData("CON")]
  [InlineData("con")]
  [InlineData("Prn")]
  [InlineData("AUX")]
  [InlineData("nul")]
  [InlineData("COM1")]
  [InlineData("com9")]
  [InlineData("LPT1")]
  [InlineData("lpt9")]
  [InlineData("con.txt")]
  [InlineData("NUL.tar.gz")]
  [InlineData("CON .txt")]
  [InlineData("COM0")]
  [InlineData("lpt0")]
  [InlineData("COM\u00B9")]
  [InlineData("COM\u00B2")]
  [InlineData("com\u00B3.txt")]
  [InlineData("LPT\u00B9")]
  [InlineData("LPT\u00B2")]
  [InlineData("LPT\u00B3")]
  public void ReservedWindowsNamesFail(string name)
  {
    Assert.Contains("reserved", ProjectNameRules.Validate(name));
  }

  [Fact]
  public void TheFirstFailingRuleWins()
  {
    Assert.Contains("at most 64", ProjectNameRules.Validate(" " + new string('/', 70)));
  }

  [Fact]
  public void UniqueDefaultCountsUpFromTwo()
  {
    Assert.Equal("First-Person", ProjectNameRules.UniqueDefault("First-Person", _ => false));
    Assert.Equal("First-Person 2", ProjectNameRules.UniqueDefault("First-Person", n => n == "First-Person"));
    string[] taken = ["First-Person", "First-Person 2", "First-Person 3"];
    Assert.Equal("First-Person 4", ProjectNameRules.UniqueDefault("First-Person", n => taken.Contains(n)));
  }

  [Fact]
  public void UniqueDefaultFitsTheRulesWhateverTheTemplateName()
  {
    var longName = new string('a', 64);
    Assert.Null(ProjectNameRules.Validate(longName));
    var cleaned = ProjectNameRules.UniqueDefault(longName, _ => false);
    Assert.Null(ProjectNameRules.Validate(cleaned));
    var suffixed = ProjectNameRules.UniqueDefault(longName, n => !n.EndsWith(" 99999", StringComparison.Ordinal));
    Assert.Null(ProjectNameRules.Validate(suffixed));
    Assert.EndsWith(" 99999", suffixed);
    Assert.Equal("ab", ProjectNameRules.UniqueDefault("a:b", _ => false));
    Assert.Equal("Project", ProjectNameRules.UniqueDefault("???", _ => false));
  }

  [Fact]
  public void AnIdWithATrailingNewlineIsSkipped()
  {
    var files = new FakeFileSystem();
    Folder(files, "ok", ManifestJson("ok", "Ok", "template"));
    Folder(files, "nl", ManifestJson("ok\\n", "Nl", "template"));
    var log = new RecordingLauncherLog();

    var contents = Load(files, log);

    Assert.Equal(new[] { "ok" }, contents.Templates.Select(e => e.Manifest.Id).ToArray());
    Assert.Contains("field 'id'", Assert.Single(log.Warnings));
  }

  [Fact]
  public void AnUnlistableTemplatesFolderIsReportedUnreadable()
  {
    var files = new FakeFileSystem();
    Folder(files, "good", ManifestJson("good", "Good", "template"));
    files.FailEnumerate = true;
    var log = new RecordingLauncherLog();

    var contents = Load(files, log);

    Assert.True(contents.Unreadable);
    Assert.True(contents.IsEmpty);
    Assert.Contains(TemplatesRoot, Assert.Single(log.Warnings));
    Assert.False(Load(new FakeFileSystem(), new RecordingLauncherLog()).Unreadable);
  }

  [Fact]
  public void ADuplicateIdKeepsTheFirstFolderAndWarns()
  {
    var files = new FakeFileSystem();
    Folder(files, "a", ManifestJson("same", "First", "template"));
    var second = Folder(files, "b", ManifestJson("same", "Second", "sample"));
    var log = new RecordingLauncherLog();

    var contents = Load(files, log);

    Assert.Equal("First", Assert.Single(contents.Templates).Manifest.Name);
    Assert.Empty(contents.Samples);
    var warning = Assert.Single(log.Warnings);
    Assert.Contains(second, warning);
    Assert.Contains("already used", warning);
  }

  [Fact]
  public void ShippedManifestsLoadWithoutWarnings()
  {
    var root = typeof(LauncherTemplateCatalogTests).Assembly
      .GetCustomAttributes<AssemblyMetadataAttribute>()
      .Single(a => a.Key == "ECS3DSourceRoot").Value!;
    var log = new RecordingLauncherLog();

    var contents = new TemplateCatalog(new PhysicalFileSystem(), log).Load(root);

    Assert.Empty(log.Warnings);
    Assert.True(contents.FolderFound);
    Assert.Equal(new[] { "empty-3d", "first-person" }, contents.Templates.Select(e => e.Manifest.Id).Order(StringComparer.Ordinal).ToArray());
    Assert.Equal(new[] { "coin-rush", "physics-playground" }, contents.Samples.Select(e => e.Manifest.Id).Order(StringComparer.Ordinal).ToArray());
    Assert.All(contents.Templates.Concat(contents.Samples), e => Assert.NotNull(e.ThumbnailPath));
    Assert.True(contents.Samples.Single(e => e.Manifest.Id == "coin-rush").Manifest.IsMultiplayer);
    Assert.False(contents.Samples.Single(e => e.Manifest.Id == "physics-playground").Manifest.IsMultiplayer);
  }
}
