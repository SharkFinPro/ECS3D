using System;
using System.IO;
using System.Linq;
using System.Text.Json.Nodes;
using System.Threading;
using System.Threading.Tasks;
using ECS3DLauncher.Services;
using ECS3DLauncher.Shell;
using ECS3DLauncher.Tabs.Templates;

namespace ECS3DManagedTests;

// Shared setup for the New Project tests: a launcher rig, an in-memory tree for the copy and the dialog builders.
public abstract class NewProjectFixture
{
  private const string ProjectJson = "{\"assets\":{\"scenes\":[{\"objects\":[{\"uuid\":\"a\",\"children\":[]}]}]}}";

  internal static readonly string Projects = LauncherRig.DefaultProjects;
  internal static readonly string Templates = Path.Combine(LauncherRig.LauncherDir, "templates");

  internal readonly LauncherRig _rig = new();
  internal readonly MemoryTreeFileSystem _files = new();
  internal readonly FakeProcessRunner _runner = new();
  internal int _counter;

  internal Guid NextUuid() => new($"00000000-0000-0000-0000-{++_counter:D12}");

  internal ProjectCreator NewCreator() => new(_files, _rig.Log, NextUuid);

  // The manifest goes to the catalog's file system and the files to be copied to the creator's.
  internal TemplateEntry AddTemplate(string id, string name, string minEngine = "0.0.1")
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

  // xUnit's own synchronization context stops a completed gate from running the awaiting code inline, so tests
  // resolve gates with none current to get a deterministic, synchronous chain.
  internal static void WithoutContext(Action action)
  {
    var saved = SynchronizationContext.Current;
    SynchronizationContext.SetSynchronizationContext(null);
    try
    {
      action();
    }
    finally
    {
      SynchronizationContext.SetSynchronizationContext(saved);
    }
  }

  internal static Task<string?> NoPick() => Task.FromResult<string?>(null);

  internal NewProjectViewModel NewDialog(
    string? location = null,
    Action<Action>? background = null,
    Action<string, bool>? created = null,
    Action? close = null,
    Func<Task<string?>>? pick = null,
    TemplateEntry? template = null,
    Func<TimeSpan, Task>? delay = null,
    Func<Func<TargetCheck>, Task<TargetCheck>>? runCheck = null,
    Action<Action>? post = null) =>
    new(
      template ?? AddTemplate("first-person", "First-Person"),
      NewCreator(),
      location ?? Projects,
      pick ?? NoPick,
      new DialogScheduling(
        background ?? (work => work()),
        post ?? (work => work()),
        delay ?? (_ => Task.CompletedTask),
        runCheck ?? (work => Task.FromResult(work()))),
      created ?? ((_, _) => { }),
      close ?? (() => { }));

  internal MainWindowViewModel NewMain(SettingsEditor? editor = null, string version = "1.2.3", Action<Action>? background = null)
  {
    _rig.AddEngine(LauncherRig.LauncherDir);
    return NewMainWithCurrentEngine(editor, version, background);
  }

  internal MainWindowViewModel NewMainWithCurrentEngine(
    SettingsEditor? editor = null,
    string version = "1.2.3",
    Action<Action>? background = null) =>
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
      background ?? (work => work()),
      NewCreator())
    {
      NewProjectCheckDelay = _ => Task.CompletedTask
    };

  internal string[] StoredPaths() =>
    ((JsonArray)JsonNode.Parse(_rig.Files.TextOf(_rig.RecentsPath)!)!["projects"]!)
      .Select(node => (string)node!["path"]!)
      .ToArray();

  internal JsonNode StoredEntry(string path) =>
    ((JsonArray)JsonNode.Parse(_rig.Files.TextOf(_rig.RecentsPath)!)!["projects"]!)
      .Single(node => (string)node!["path"]! == path)!;

  internal static string Folder(string name) => Path.Combine(Projects, name);
}
