using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using ECS3DLauncher.Services;
using ECS3DLauncher.Shell;
using ECS3DLauncher.Tabs.Templates;
using Xunit;

namespace ECS3DManagedTests;

// A tree of text files and folders in memory that can copy, move and delete, with separators folded to '/'.
internal sealed class MemoryTreeFileSystem : IFileSystem
{
  private readonly Dictionary<string, string> _files = [];
  private readonly HashSet<string> _directories = [];

  // Thrown when a file whose name ends with CopyFailureSuffix is copied.
  public Exception? CopyFailure { get; set; }

  public string CopyFailureSuffix { get; set; } = "A.cs";

  // Looking at this folder or anything below it throws, as an unreadable share would.
  public string? UnreadableUnder { get; set; }

  private static string Norm(string path)
  {
    var folded = path.Replace('\\', '/');
    return folded.Length > 1 ? folded.TrimEnd('/') : folded;
  }

  private static string Parent(string path)
  {
    var cut = path.LastIndexOf('/');
    return cut < 0 ? "" : cut == 0 ? "/" : path[..cut];
  }

  private string Seen(string path)
  {
    var folded = Norm(path);
    if (UnreadableUnder is { } blocked && (folded == Norm(blocked) || folded.StartsWith(Norm(blocked) + "/", StringComparison.Ordinal)))
    {
      throw new UnauthorizedAccessException("cannot look here");
    }

    return folded;
  }

  public void AddDirectory(string path)
  {
    for (var folder = Norm(path); folder.Length > 0 && _directories.Add(folder); folder = Parent(folder))
    {
      // Each ancestor is added until one is already known.
    }
  }

  public void Seed(string path, string contents)
  {
    _files[Norm(path)] = contents;
    AddDirectory(Parent(Norm(path)));
  }

  public bool FileExists(string path) => _files.ContainsKey(Seen(path));

  public bool DirectoryExists(string path) => _directories.Contains(Seen(path));

  public IReadOnlyList<string> EnumerateDirectories(string path) =>
    _directories.Where(directory => Parent(directory) == Norm(path)).Order(StringComparer.Ordinal).ToList();

  public IReadOnlyList<string> EnumerateFiles(string path) =>
    _files.Keys.Where(file => Parent(file) == Norm(path)).Order(StringComparer.Ordinal).ToList();

  public string ReadAllText(string path) =>
    _files.TryGetValue(Norm(path), out var text) ? text : throw new FileNotFoundException(path);

  public void WriteAllTextAtomic(string path, string contents) => Seed(path, contents);

  public void CreateDirectory(string path) => AddDirectory(path);

  public void CopyFile(string source, string destination)
  {
    if (CopyFailure is not null && Norm(source).EndsWith(CopyFailureSuffix, StringComparison.Ordinal))
    {
      throw CopyFailure;
    }

    if (_files.ContainsKey(Norm(destination)))
    {
      throw new IOException("exists");
    }

    Seed(destination, ReadAllText(source));
  }

  public void MoveDirectory(string source, string destination)
  {
    var from = Norm(source);
    var to = Norm(destination);
    if (_directories.Contains(to))
    {
      throw new IOException("exists");
    }

    foreach (var directory in _directories.Where(item => item == from || item.StartsWith(from + "/")).ToList())
    {
      _directories.Remove(directory);
      _directories.Add(to + directory[from.Length..]);
    }

    foreach (var file in _files.Keys.Where(item => item.StartsWith(from + "/")).ToList())
    {
      _files[to + file[from.Length..]] = _files[file];
      _files.Remove(file);
    }

    AddDirectory(Parent(to));
  }

  public void DeleteDirectory(string path, bool recursive)
  {
    var folder = Norm(path);
    var inside = _directories.Where(item => item.StartsWith(folder + "/")).ToList();
    var held = _files.Keys.Where(item => item.StartsWith(folder + "/")).ToList();
    if (!recursive && (inside.Count > 0 || held.Count > 0))
    {
      throw new IOException("not empty");
    }

    inside.ForEach(item => _directories.Remove(item));
    held.ForEach(item => _files.Remove(item));
    _directories.Remove(folder);
  }
}

public class LauncherNewProjectTests
{
  private const string ProjectJson = "{\"assets\":{\"scenes\":[{\"objects\":[{\"uuid\":\"a\",\"children\":[]}]}]}}";

  private static readonly string Projects = LauncherRig.DefaultProjects;
  private static readonly string Templates = Path.Combine(LauncherRig.LauncherDir, "templates");

  private readonly LauncherRig _rig = new();
  private readonly MemoryTreeFileSystem _files = new();
  private readonly FakeProcessRunner _runner = new();
  private int _counter;

  private Guid NextUuid() => new($"00000000-0000-0000-0000-{++_counter:D12}");

  private ProjectCreator NewCreator() => new(_files, _rig.Log, NextUuid);

  // The manifest goes to the catalog's file system and the files to be copied to the creator's.
  private TemplateEntry AddTemplate(string id, string name, string minEngine = "0.0.1")
  {
    var folder = Path.Combine(Templates, id);
    var json = "{\"id\":\"" + id + "\",\"name\":\"" + name + "\",\"description\":\"About " + name
      + "\",\"category\":\"template\",\"tags\":[],\"thumbnail\":\"thumbnail.png\",\"entryScene\":\"Main\","
      + "\"minEngineVersion\":\"" + minEngine + "\"}";
    _rig.Files.AddDirectory(Templates);
    _rig.Files.AddDirectory(folder);
    _rig.Files.Seed(Path.Combine(folder, "template.json"), json);
    _files.Seed(Path.Combine(folder, "template.json"), json);
    _files.Seed(Path.Combine(folder, "project.ecs3d"), ProjectJson);
    _files.Seed(Path.Combine(folder, "scripts", "A.cs"), "class A {}");
    var manifest = new TemplateManifest(id, name, "About " + name, "template", [], "thumbnail.png", "Main", minEngine);
    return new TemplateEntry(manifest, folder, null);
  }

  private static Task<string?> NoPick() => Task.FromResult<string?>(null);

  private NewProjectViewModel NewDialog(
    string? location = null,
    Action<Action>? background = null,
    Action<string, bool>? created = null,
    Action? close = null,
    Func<Task<string?>>? pick = null,
    TemplateEntry? template = null) =>
    new(
      template ?? AddTemplate("first-person", "First-Person"),
      NewCreator(),
      location ?? Projects,
      pick ?? NoPick,
      background ?? (work => work()),
      work => work(),
      created ?? ((_, _) => { }),
      close ?? (() => { }));

  private MainWindowViewModel NewMain(SettingsEditor? editor = null, string version = "1.2.3")
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    return NewMainWithCurrentEngine(editor, version);
  }

  private MainWindowViewModel NewMainWithCurrentEngine(SettingsEditor? editor = null, string version = "1.2.3") =>
    new(
      editor ?? _rig.NewEditor(),
      _rig.NewRecents(),
      _runner,
      new EarlyExitWatcher(ShellFixtures.UserData, TimeSpan.FromHours(1)),
      _rig.NewCatalog(),
      _ => null,
      version,
      action => action(),
      () => _rig.Time.Now,
      _files,
      work => work(),
      NewCreator());

  private string[] StoredPaths() =>
    ((JsonArray)JsonNode.Parse(_rig.Files.TextOf(_rig.RecentsPath)!)!["projects"]!)
      .Select(node => (string)node!["path"]!)
      .ToArray();

  private JsonNode StoredEntry(string path) =>
    ((JsonArray)JsonNode.Parse(_rig.Files.TextOf(_rig.RecentsPath)!)!["projects"]!)
      .Single(node => (string)node!["path"]! == path)!;

  private static string Folder(string name) => Path.Combine(Projects, name);

  [Fact]
  public void TheNameDefaultsToTheTemplateNameWhileItIsFree()
  {
    var dialog = NewDialog();

    Assert.Equal("First-Person", dialog.ProjectName);
    Assert.Equal("First-Person", dialog.TemplateName);
    Assert.Equal("About First-Person", dialog.TemplateDescription);
    Assert.Equal(Projects, dialog.Location);
    Assert.True(dialog.OpenInEditor);
    Assert.True(dialog.CanCreate);
  }

  [Fact]
  public void ATakenDefaultNameGetsTheNextFreeNumber()
  {
    _files.Seed(Path.Combine(Folder("First-Person"), "project.ecs3d"), "{}");
    Assert.Equal("First-Person 2", NewDialog().ProjectName);

    _files.Seed(Path.Combine(Folder("First-Person 2"), "project.ecs3d"), "{}");
    Assert.Equal("First-Person 3", NewDialog().ProjectName);
  }

  [Fact]
  public void AnEmptyFolderDoesNotTakeTheDefaultName()
  {
    _files.AddDirectory(Folder("First-Person"));

    Assert.Equal("First-Person", NewDialog().ProjectName);
  }

  [Theory]
  [InlineData("", "Enter a project name.")]
  [InlineData("LONG", "Use at most 64 characters.")]
  [InlineData("a/b", "The name cannot contain")]
  [InlineData("name.", "start or end with a space or a dot")]
  [InlineData("CON", "reserved name")]
  public void AnInvalidNameShowsItsMessageAndBlocksCreate(string name, string message)
  {
    var dialog = NewDialog();
    Assert.True(dialog.CanCreate);

    dialog.ProjectName = name == "LONG" ? new string('a', 65) : name;

    Assert.Contains(message, dialog.NameError);
    Assert.True(dialog.HasNameError);
    Assert.False(dialog.CanCreate);
    Assert.False(dialog.CreateCommand.CanExecute(null));
    Assert.Equal("", dialog.TargetPreview);
    Assert.Null(dialog.TargetState);

    dialog.ProjectName = "Fine";

    Assert.Null(dialog.NameError);
    Assert.False(dialog.HasNameError);
    Assert.True(dialog.CanCreate);
    Assert.True(dialog.CreateCommand.CanExecute(null));
  }

  [Fact]
  public void ThePreviewShowsTheProjectFileInTheNewFolder()
  {
    var dialog = NewDialog();
    dialog.ProjectName = "Game";

    Assert.Equal(Path.Combine(Projects, "Game", "project.ecs3d"), dialog.TargetPreview);
    Assert.Equal("Folder name is free", dialog.TargetMessage);
    Assert.True(dialog.IsTargetFree);
    Assert.False(dialog.HasTargetProblem);
  }

  [Fact]
  public void OnlyAFreeTargetEnablesCreate()
  {
    _files.Seed(Path.Combine(Folder("Used"), "x.txt"), "x");
    _files.Seed(Folder("Taken"), "a file");
    var dialog = NewDialog();

    dialog.ProjectName = "Used";
    Assert.Equal(ProjectTargetState.NotEmpty, dialog.TargetState);
    Assert.Equal("A folder with this name already exists and is not empty.", dialog.TargetMessage);
    Assert.True(dialog.HasTargetProblem);
    Assert.False(dialog.IsTargetFree);
    Assert.False(dialog.CanCreate);

    dialog.ProjectName = "Taken";
    Assert.Equal(ProjectTargetState.NotAFolder, dialog.TargetState);
    Assert.Equal("A file with this name already exists.", dialog.TargetMessage);
    Assert.False(dialog.CanCreate);

    _files.UnreadableUnder = Folder("Locked");
    dialog.ProjectName = "Locked";
    Assert.Equal(ProjectTargetState.Unavailable, dialog.TargetState);
    Assert.Equal("This location cannot be used.", dialog.TargetMessage);
    Assert.False(dialog.CanCreate);

    dialog.ProjectName = "Fresh";
    Assert.Equal(ProjectTargetState.Free, dialog.TargetState);
    Assert.True(dialog.CanCreate);
  }

  [Fact]
  public void ALocationThatIsAFileOrBlankOrMalformedBlocksCreate()
  {
    _files.Seed("/docs/afile", "x");
    var dialog = NewDialog();
    Assert.True(dialog.CanCreate);

    dialog.Location = "/docs/afile";
    Assert.Equal("That location is a file, not a folder.", dialog.LocationError);
    Assert.False(dialog.CanCreate);
    Assert.Null(dialog.TargetState);

    dialog.Location = "  ";
    Assert.Equal("Enter a location.", dialog.LocationError);
    Assert.False(dialog.CanCreate);

    dialog.Location = "bad\0path";
    Assert.Equal("This location cannot be used.", dialog.LocationError);
    Assert.False(dialog.CanCreate);

    dialog.Location = "/docs/elsewhere";
    Assert.Null(dialog.LocationError);
    Assert.False(dialog.HasLocationError);
    Assert.True(dialog.CanCreate);
  }

  [Fact]
  public void ChangingTheLocationChecksTheNameThereAgain()
  {
    _files.Seed(Path.Combine("/docs/other", "Game", "x.txt"), "x");
    var dialog = NewDialog();
    dialog.ProjectName = "Game";
    Assert.True(dialog.CanCreate);

    dialog.Location = "/docs/other";

    Assert.Equal(ProjectTargetState.NotEmpty, dialog.TargetState);
    Assert.Equal(Path.Combine("/docs/other", "Game", "project.ecs3d"), dialog.TargetPreview);
    Assert.False(dialog.CanCreate);
  }

  [Fact]
  public void BusyDisablesCreateAndCancelUntilTheCopyFinishes()
  {
    var work = new List<Action>();
    var created = new List<(string Folder, bool Open)>();
    var closed = 0;
    var dialog = NewDialog(background: work.Add, created: (folder, open) => created.Add((folder, open)), close: () => closed++);
    dialog.ProjectName = "Game";

    dialog.CreateCommand.Execute(null);

    Assert.True(dialog.IsBusy);
    Assert.False(dialog.CanCreate);
    Assert.False(dialog.CreateCommand.CanExecute(null));
    Assert.False(dialog.CancelCommand.CanExecute(null));
    Assert.False(dialog.BrowseCommand.CanExecute(null));
    dialog.CancelCommand.Execute(null);
    dialog.CreateCommand.Execute(null);
    Assert.Equal(0, closed);
    Assert.Single(work);
    Assert.False(_files.DirectoryExists(Folder("Game")));

    work[0]();

    Assert.False(dialog.IsBusy);
    Assert.Equal(new[] { (Folder("Game"), true) }, created.ToArray());
    Assert.True(_files.FileExists(Path.Combine(Folder("Game"), "project.ecs3d")));
    Assert.True(dialog.CancelCommand.CanExecute(null));
    dialog.CancelCommand.Execute(null);
    Assert.Equal(1, closed);
  }

  [Fact]
  public void TheEditorChoiceReachesTheCallback()
  {
    var created = new List<bool>();
    var dialog = NewDialog(created: (_, open) => created.Add(open));
    dialog.ProjectName = "Game";
    dialog.OpenInEditor = false;

    dialog.CreateCommand.Execute(null);

    Assert.Equal(new[] { false }, created.ToArray());
  }

  [Theory]
  [InlineData("denied")]
  [InlineData("unexpected")]
  public void AFailedCopyKeepsTheDialogOpenWithTheReason(string kind)
  {
    _files.CopyFailure = kind == "denied"
      ? new UnauthorizedAccessException("access denied")
      : new InvalidOperationException("boom");
    var created = 0;
    var dialog = NewDialog(created: (_, _) => created++);
    dialog.ProjectName = "Game";

    dialog.CreateCommand.Execute(null);

    Assert.Equal(0, created);
    Assert.False(dialog.IsBusy);
    Assert.True(dialog.HasCreateError);
    Assert.Contains(kind == "denied" ? "access denied" : "boom", dialog.CreateError);
    Assert.True(dialog.CanCreate);
    Assert.True(dialog.CancelCommand.CanExecute(null));
    Assert.False(_files.DirectoryExists(Folder("Game")));

    dialog.ProjectName = "Game2";
    Assert.Null(dialog.CreateError);
    Assert.False(dialog.HasCreateError);
  }

  [Fact]
  public async Task BrowseSetsTheChosenFolderAndIgnoresACancel()
  {
    var answers = new Queue<string?>(["/docs/picked", null, ""]);
    var dialog = NewDialog(pick: () => Task.FromResult(answers.Dequeue()));

    dialog.BrowseCommand.Execute(null);
    await Task.Yield();
    Assert.Equal("/docs/picked", dialog.Location);

    dialog.BrowseCommand.Execute(null);
    await Task.Yield();
    Assert.Equal("/docs/picked", dialog.Location);
    Assert.Null(dialog.CreateError);

    dialog.BrowseCommand.Execute(null);
    await Task.Yield();
    Assert.Equal("/docs/picked", dialog.Location);
    Assert.Equal("Choose a folder on this computer.", dialog.CreateError);
  }

  [Fact]
  public void ACardClickOpensTheDialogForThatTemplate()
  {
    AddTemplate("empty-3d", "Empty 3D");
    AddTemplate("first-person", "First-Person");
    var model = NewMain();
    Assert.Null(model.NewProject);

    model.Templates.OpenCommand.Execute(model.Templates.Templates.Single(card => card.Id == "first-person"));

    Assert.NotNull(model.NewProject);
    Assert.Equal("First-Person", model.NewProject.TemplateName);
    Assert.Equal(LauncherRig.DefaultProjects, model.NewProject.Location);
    Assert.Equal("projects", model.ActiveNav);
  }

  [Fact]
  public void ACardNeedingANewerEngineDoesNotOpenTheDialog()
  {
    AddTemplate("first-person", "First-Person");
    AddTemplate("future", "Future", minEngine: "9.0");
    var model = NewMain();
    var future = model.Templates.Templates.Single(card => card.Id == "future");
    var usable = model.Templates.Templates.Single(card => card.Id == "first-person");

    Assert.False(model.Templates.OpenCommand.CanExecute(future));
    model.Templates.OpenCommand.Execute(future);
    Assert.Null(model.NewProject);

    Assert.True(model.Templates.OpenCommand.CanExecute(usable));
    model.Templates.OpenCommand.Execute(usable);
    Assert.NotNull(model.NewProject);
  }

  [Fact]
  public void TheDialogStartsInTheProjectsFolderFromTheSettings()
  {
    AddTemplate("empty-3d", "Empty 3D");
    var editor = _rig.NewEditor();
    editor.SetProjectsDirectory("/custom/projects");
    var model = NewMain(editor);

    model.NewProjectCommand.Execute(null);

    Assert.Equal("/custom/projects", model.NewProject!.Location);
  }

  [Fact]
  public void TheHeaderButtonPreselectsEmpty3DOnAnyTab()
  {
    AddTemplate("first-person", "First-Person");
    AddTemplate("empty-3d", "Empty 3D");
    var model = NewMain();
    model.ActiveNav = "samples";

    model.NewProjectCommand.Execute(null);

    Assert.Equal("Empty 3D", model.NewProject!.TemplateName);
    Assert.Equal("samples", model.ActiveNav);
    Assert.Null(model.ToastMessage);
  }

  [Fact]
  public void WithoutEmpty3DTheFirstUsableTemplateIsUsed()
  {
    AddTemplate("empty-3d", "Empty 3D", minEngine: "9.0");
    AddTemplate("first-person", "First-Person");

    var model = NewMain();
    model.NewProjectCommand.Execute(null);

    Assert.Equal("First-Person", model.NewProject!.TemplateName);
  }

  [Fact]
  public void TheFirstRunCardOpensTheDialogToo()
  {
    AddTemplate("empty-3d", "Empty 3D");
    var model = NewMain();

    model.Projects.NewProjectCommand.Execute(null);

    Assert.Equal("Empty 3D", model.NewProject!.TemplateName);
    Assert.Same(model.Projects, model.CurrentTab);
  }

  [Fact]
  public void NoTemplatesMeansNoDialogAndAToast()
  {
    var model = NewMain();

    model.NewProjectCommand.Execute(null);

    Assert.Null(model.NewProject);
    Assert.Contains(Templates, model.ToastMessage);
  }

  [Fact]
  public void OnlyGatedTemplatesMeansNoDialogAndAToast()
  {
    AddTemplate("future", "Future", minEngine: "9.0");
    var model = NewMain();

    model.NewProjectCommand.Execute(null);

    Assert.Null(model.NewProject);
    Assert.Contains("newer engine", model.ToastMessage);
  }

  [Fact]
  public void CancelClosesTheDialog()
  {
    AddTemplate("empty-3d", "Empty 3D");
    var model = NewMain();
    model.NewProjectCommand.Execute(null);

    model.NewProject!.CancelCommand.Execute(null);

    Assert.Null(model.NewProject);
    Assert.Empty(_files.EnumerateDirectories(Projects));
  }

  [Fact]
  public void CreatingRecordsTheTemplateAtTheTopAndOpensTheEditorOnce()
  {
    AddTemplate("empty-3d", "Empty 3D");
    _rig.Files.Seed(_rig.RecentsPath, "{\"version\":1,\"projects\":[{\"path\":\"/work/alpha\",\"name\":\"alpha\","
      + "\"lastOpenedUtc\":\"2026-10-18T10:00:00Z\"}]}");
    var model = NewMain();
    model.NewProjectCommand.Execute(null);
    model.NewProject!.ProjectName = "Game";

    model.NewProject.CreateCommand.Execute(null);

    Assert.Null(model.NewProject);
    Assert.True(_files.FileExists(Path.Combine(Folder("Game"), "project.ecs3d")));
    var stored = StoredPaths();
    Assert.Equal(2, stored.Length);
    Assert.EndsWith("Game", stored[0]);
    Assert.Equal("empty-3d", (string)StoredEntry(stored[0])["template"]!);
    Assert.Equal(stored[0], model.Projects.Projects[0].Path);
    var launch = Assert.Single(_runner.Launches);
    Assert.EndsWith("ECS3DEditor", launch.FileName);
    Assert.Equal(new[] { "--project", Folder("Game") }, launch.Arguments.ToArray());
  }

  [Fact]
  public void AnUncheckedBoxRecordsTheProjectWithoutOpeningIt()
  {
    AddTemplate("empty-3d", "Empty 3D");
    var model = NewMain();
    model.NewProjectCommand.Execute(null);
    model.NewProject!.OpenInEditor = false;

    model.NewProject.CreateCommand.Execute(null);

    Assert.Null(model.NewProject);
    Assert.Empty(_runner.Launches);
    Assert.Null(model.ToastMessage);
    Assert.Equal("empty-3d", (string)StoredEntry(Assert.Single(StoredPaths()))["template"]!);
  }

  [Fact]
  public void AMissingEditorStillCreatesAndRecordsTheProjectAndSaysSo()
  {
    AddTemplate("empty-3d", "Empty 3D");
    _rig.AddEngine(LauncherRig.LauncherDir, EngineExecutable.Editor);
    var model = NewMainWithCurrentEngine();
    model.NewProjectCommand.Execute(null);
    var folder = Path.Combine(Projects, model.NewProject!.ProjectName);

    model.NewProject.CreateCommand.Execute(null);

    Assert.Null(model.NewProject);
    Assert.Empty(_runner.Launches);
    Assert.True(_files.FileExists(Path.Combine(folder, "project.ecs3d")));
    Assert.Equal("empty-3d", (string)StoredEntry(Assert.Single(StoredPaths()))["template"]!);
    Assert.Contains(folder, model.ToastMessage);
    Assert.Contains("ECS3DEditor", model.ToastMessage);
  }

  [Fact]
  public void AFailedCreateKeepsTheDialogAndRecordsNothing()
  {
    AddTemplate("empty-3d", "Empty 3D");
    _files.CopyFailure = new UnauthorizedAccessException("access denied");
    var model = NewMain();
    model.NewProjectCommand.Execute(null);
    var dialog = model.NewProject!;

    dialog.CreateCommand.Execute(null);

    Assert.Same(dialog, model.NewProject);
    Assert.Contains("access denied", dialog.CreateError);
    Assert.False(dialog.IsBusy);
    Assert.Empty(_runner.Launches);
    Assert.Null(_rig.Files.TextOf(_rig.RecentsPath));
    Assert.Empty(model.Projects.Projects);
  }
}
