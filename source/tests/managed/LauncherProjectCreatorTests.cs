using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text.Json.Nodes;
using ECS3DLauncher.Services;
using Xunit;

namespace ECS3DManagedTests;

// Delegates to the real file system and fails on request.
internal sealed class FailingFileSystem : IFileSystem
{
  private readonly PhysicalFileSystem _inner = new();
  public string? FailCopyOf { get; set; }

  public bool FailEnumerateFiles { get; set; }

  public bool FailMove { get; set; }

  public bool FailWrite { get; set; }

  public bool FailDelete { get; set; }

  public bool FileExists(string path) => _inner.FileExists(path);

  public bool DirectoryExists(string path) => _inner.DirectoryExists(path);

  public IReadOnlyList<string> EnumerateDirectories(string path) => _inner.EnumerateDirectories(path);

  public IReadOnlyList<string> EnumerateFiles(string path) =>
    FailEnumerateFiles ? throw new UnauthorizedAccessException("listing denied") : _inner.EnumerateFiles(path);

  public string ReadAllText(string path) => _inner.ReadAllText(path);

  public void WriteAllTextAtomic(string path, string contents)
  {
    if (FailWrite)
    {
      throw new IOException("disk write failed");
    }

    _inner.WriteAllTextAtomic(path, contents);
  }

  public void CreateDirectory(string path) => _inner.CreateDirectory(path);

  public void CopyFile(string source, string destination)
  {
    if (FailCopyOf is not null && source.EndsWith(FailCopyOf, StringComparison.Ordinal))
    {
      throw new UnauthorizedAccessException("access to x is denied");
    }

    _inner.CopyFile(source, destination);
  }

  public void MoveDirectory(string source, string destination)
  {
    if (FailMove)
    {
      throw new UnauthorizedAccessException("move to x is denied");
    }

    _inner.MoveDirectory(source, destination);
  }

  public void DeleteDirectory(string path, bool recursive)
  {
    if (FailDelete)
    {
      throw new IOException("delete failed");
    }

    _inner.DeleteDirectory(path, recursive);
  }
}

public class LauncherProjectCreatorTests : IDisposable
{
  private const string ProjectJson = """
    {
      "assets": {
        "models": [{"uuid": "model-1", "path": "m.obj"}],
        "textures": [{"uuid": "texture-1"}],
        "scripts": [{"uuid": "script-1"}],
        "prefabs": [{"name": "P", "uuid": "prefab-1", "body": {"name": "B", "uuid": "body-1", "children": [], "components": {}}}],
        "scenes": [
          {"name": "A", "uuid": "scene-a", "scripts": [], "objects": [
            {"name": "Root", "uuid": "obj-1", "scripts": [], "components": {"t": {"x": 1.5, "y": -10, "z": 0.30000000000000004, "w": 1e-7}},
             "children": [{"name": "C1", "uuid": "obj-2", "components": {}, "children": [
               {"name": "C2", "uuid": "obj-3", "components": {}, "children": [{"name": "C3", "uuid": "obj-4", "components": {}, "children": []}]}]}]}
          ]},
          {"name": "B", "uuid": "scene-b", "scripts": [], "objects": [{"name": "Other", "uuid": "obj-5", "components": {}, "children": []}]}
        ]
      },
      "currentSceneUUID": "scene-a"
    }
    """;

  private readonly string _root = Path.Combine(Path.GetTempPath(), "ecs3d-creator-" + Guid.NewGuid().ToString("N"));
  private readonly RecordingLauncherLog _log = new();
  private int _counter;

  public void Dispose()
  {
    var readOnly = Path.Combine(_root, "loc", "Game", "project.ecs3d");
    foreach (var file in new[] { Path.Combine(_root, "template", "project.ecs3d"), readOnly })
    {
      if (File.Exists(file))
      {
        File.SetAttributes(file, FileAttributes.Normal);
      }
    }

    if (Directory.Exists(_root))
    {
      Directory.Delete(_root, true);
    }

    GC.SuppressFinalize(this);
  }

  private Guid NextUuid() => new($"00000000-0000-0000-0000-{++_counter:D12}");

  private static byte[] AllByteValues() => Enumerable.Range(0, 256).Select(i => (byte)i).ToArray();

  private string Location => Path.Combine(_root, "loc");

  private TemplateEntry MakeTemplate(string projectJson = ProjectJson)
  {
    var folder = Path.Combine(_root, "template");
    Directory.CreateDirectory(Path.Combine(folder, "scripts"));
    Directory.CreateDirectory(Path.Combine(folder, "assets", "models"));
    File.WriteAllText(Path.Combine(folder, "template.json"), "{}");
    File.WriteAllText(Path.Combine(folder, "project.ecs3d"), projectJson);
    File.WriteAllBytes(Path.Combine(folder, "thumbnail.png"), AllByteValues());
    File.WriteAllText(Path.Combine(folder, "scripts", "A.cs"), "class A {}");
    File.WriteAllText(Path.Combine(folder, "scripts", "template.json"), "nested");
    var manifest = new TemplateManifest("t", "T", "d", "template", ["a"], "thumbnail.png", "Main", "0.0.1");
    return new TemplateEntry(manifest, folder, null);
  }

  private ProjectCreator NewCreator(IFileSystem? files = null) =>
    new(files ?? new PhysicalFileSystem(), _log, NextUuid);

  private static string HiddenOf(string location, string name) =>
    Path.Combine(location, ProjectCreator.HiddenFolderName(name));

  private static List<JsonObject> ObjectsOf(JsonNode project)
  {
    var objects = new List<JsonObject>();
    foreach (var scene in project["assets"]!["scenes"]!.AsArray())
    {
      CollectObjects(scene!["objects"]!.AsArray(), objects);
    }

    return objects;
  }

  private static void CollectObjects(JsonArray list, List<JsonObject> into)
  {
    foreach (var item in list.OfType<JsonObject>())
    {
      into.Add(item);
      if (item["children"] is JsonArray children)
      {
        CollectObjects(children, into);
      }
    }
  }

  private static void BlankObjectUuids(JsonNode project)
  {
    foreach (var item in ObjectsOf(project))
    {
      item["uuid"] = "";
    }
  }

  [Fact]
  public void CreateCopiesTheTreeAndSkipsTheManifest()
  {
    var result = NewCreator().Create(MakeTemplate(), Location, "My Game");

    var target = Path.Combine(Location, "My Game");
    Assert.True(result.Succeeded);
    Assert.Equal(target, result.ProjectFolder);
    Assert.Equal(AllByteValues(), File.ReadAllBytes(Path.Combine(target, "thumbnail.png")));
    Assert.Equal("class A {}", File.ReadAllText(Path.Combine(target, "scripts", "A.cs")));
    Assert.True(File.Exists(Path.Combine(target, "project.ecs3d")));
    Assert.True(Directory.Exists(Path.Combine(target, "assets", "models")));
    Assert.False(File.Exists(Path.Combine(target, "template.json")));
    Assert.True(File.Exists(Path.Combine(target, "scripts", "template.json")));
    Assert.False(Directory.Exists(HiddenOf(Location, "My Game")));
  }

  [Fact]
  public void RewriteChangesOnlyObjectUuids()
  {
    var output = ProjectCreator.RewriteObjectUuids(ProjectJson, NextUuid);
    var before = JsonNode.Parse(ProjectJson)!;
    var after = JsonNode.Parse(output)!;

    var objects = ObjectsOf(after);
    var oldIds = ObjectsOf(before).Select(o => o["uuid"]!.GetValue<string>()).ToHashSet();
    Assert.Equal(5, objects.Count);
    Assert.Equal(5, objects.Select(o => o["uuid"]!.GetValue<string>()).Distinct().Count());
    Assert.All(objects, o => Assert.StartsWith("00000000-0000-0000-0000-", o["uuid"]!.GetValue<string>()));
    Assert.DoesNotContain(objects, o => oldIds.Contains(o["uuid"]!.GetValue<string>()));
    Assert.Equal("scene-a", after["assets"]!["scenes"]![0]!["uuid"]!.GetValue<string>());
    Assert.Equal("scene-b", after["assets"]!["scenes"]![1]!["uuid"]!.GetValue<string>());
    Assert.Equal("prefab-1", after["assets"]!["prefabs"]![0]!["uuid"]!.GetValue<string>());
    Assert.Equal("body-1", after["assets"]!["prefabs"]![0]!["body"]!["uuid"]!.GetValue<string>());
    Assert.Equal("model-1", after["assets"]!["models"]![0]!["uuid"]!.GetValue<string>());
    Assert.Equal("scene-a", after["currentSceneUUID"]!.GetValue<string>());

    BlankObjectUuids(before);
    BlankObjectUuids(after);
    Assert.True(JsonNode.DeepEquals(before, after));
    Assert.Contains("1e-7", output);
    Assert.Contains("0.30000000000000004", output);
  }

  [Fact]
  public void AnObjectWithoutAUuidGetsOne()
  {
    var json = "{\"assets\":{\"scenes\":[{\"uuid\":\"s\",\"objects\":[{\"name\":\"N\"}]}]}}";

    var after = JsonNode.Parse(ProjectCreator.RewriteObjectUuids(json, NextUuid))!;

    Assert.Equal("00000000-0000-0000-0000-000000000001", ObjectsOf(after)[0]["uuid"]!.GetValue<string>());
    Assert.Equal("s", after["assets"]!["scenes"]![0]!["uuid"]!.GetValue<string>());
  }

  [Theory]
  [InlineData("{")]
  [InlineData("{\"assets\":{}}")]
  public void AnUnusableProjectFileFailsAndLeavesNothing(string projectJson)
  {
    var result = NewCreator().Create(MakeTemplate(projectJson), Location, "Game");

    Assert.False(result.Succeeded);
    Assert.StartsWith("Could not create the project:", result.Error);
    Assert.False(Directory.Exists(HiddenOf(Location, "Game")));
    Assert.False(Directory.Exists(Path.Combine(Location, "Game")));
    Assert.Single(_log.Warnings);
  }

  [Fact]
  public void EveryShippedProjectFileRewritesCleanly()
  {
    var root = typeof(LauncherProjectCreatorTests).Assembly
      .GetCustomAttributes<AssemblyMetadataAttribute>()
      .Single(a => a.Key == "ECS3DSourceRoot").Value!;
    var files = Directory.GetFiles(Path.Combine(root, "templates"), "project.ecs3d", SearchOption.AllDirectories);
    Assert.NotEmpty(files);

    foreach (var file in files)
    {
      var text = File.ReadAllText(file);
      var before = JsonNode.Parse(text)!;
      var after = JsonNode.Parse(ProjectCreator.RewriteObjectUuids(text, NextUuid))!;
      var oldObjects = ObjectsOf(before);
      var newObjects = ObjectsOf(after);

      Assert.Equal(oldObjects.Count, newObjects.Count);
      for (var i = 0; i < newObjects.Count; i++)
      {
        Assert.NotEqual(oldObjects[i]["uuid"]!.GetValue<string>(), newObjects[i]["uuid"]!.GetValue<string>());
      }

      BlankObjectUuids(before);
      BlankObjectUuids(after);
      Assert.True(JsonNode.DeepEquals(before, after), file);
    }
  }

  [Fact]
  public void ANonEmptyTargetIsRefusedAndUntouched()
  {
    var template = MakeTemplate();
    var target = Path.Combine(Location, "Game");
    Directory.CreateDirectory(target);
    File.WriteAllText(Path.Combine(target, "keep.txt"), "keep");

    var result = NewCreator().Create(template, Location, "Game");

    Assert.False(result.Succeeded);
    Assert.Equal($"{target} already exists and is not empty.", result.Error);
    Assert.Equal("keep", File.ReadAllText(Path.Combine(target, "keep.txt")));
    Assert.False(File.Exists(Path.Combine(target, "project.ecs3d")));
    Assert.False(Directory.Exists(HiddenOf(Location, "Game")));
  }

  [Fact]
  public void AnEmptyTargetIsReplaced()
  {
    var template = MakeTemplate();
    var target = Path.Combine(Location, "Game");
    Directory.CreateDirectory(target);

    var result = NewCreator().Create(template, Location, "Game");

    Assert.True(result.Succeeded);
    Assert.True(File.Exists(Path.Combine(target, "project.ecs3d")));
  }

  [Fact]
  public void AFileAtTheTargetIsRefused()
  {
    var template = MakeTemplate();
    Directory.CreateDirectory(Location);
    var target = Path.Combine(Location, "Game");
    File.WriteAllText(target, "file");

    var result = NewCreator().Create(template, Location, "Game");

    Assert.Equal($"{target} already exists as a file.", result.Error);
    Assert.Equal("file", File.ReadAllText(target));
  }

  [Fact]
  public void AMissingLocationIsCreated()
  {
    var template = MakeTemplate();
    var deep = Path.Combine(_root, "a", "b");

    var result = NewCreator().Create(template, deep, "Game");

    Assert.True(result.Succeeded);
    Assert.True(Directory.Exists(deep));
  }

  [Theory]
  [InlineData("")]
  [InlineData("CON")]
  [InlineData("a/b")]
  public void AnInvalidNameTouchesNothing(string name)
  {
    var result = NewCreator().Create(MakeTemplate(), Location, name);

    Assert.NotNull(ProjectNameRules.Validate(name));
    Assert.Equal(ProjectNameRules.Validate(name), result.Error);
    Assert.False(Directory.Exists(Location));
  }

  [Fact]
  public void AStaleHiddenFolderIsReplaced()
  {
    var template = MakeTemplate();
    var hidden = HiddenOf(Location, "Game");
    Directory.CreateDirectory(hidden);
    File.WriteAllText(Path.Combine(hidden, "junk.txt"), "junk");

    var result = NewCreator().Create(template, Location, "Game");

    Assert.True(result.Succeeded);
    Assert.False(File.Exists(Path.Combine(result.ProjectFolder!, "junk.txt")));
    Assert.True(File.Exists(Path.Combine(result.ProjectFolder!, "project.ecs3d")));
    Assert.False(Directory.Exists(hidden));
  }

  [Fact]
  public void WithoutAFailureTheFailingFileSystemSucceeds()
  {
    var result = NewCreator(new FailingFileSystem()).Create(MakeTemplate(), Location, "Game");

    Assert.True(result.Succeeded);
    Assert.Empty(_log.Warnings);
  }

  [Theory]
  [InlineData("copy", "You do not have permission to use that folder or the template.")]
  [InlineData("move", "You do not have permission to use that folder or the template.")]
  [InlineData("write", "disk write failed")]
  public void AFailureRemovesTheHiddenFolder(string kind, string message)
  {
    var files = new FailingFileSystem
    {
      FailCopyOf = kind == "copy" ? "A.cs" : null,
      FailMove = kind == "move",
      FailWrite = kind == "write"
    };

    var result = NewCreator(files).Create(MakeTemplate(), Location, "Game");

    Assert.False(result.Succeeded);
    Assert.Contains(message, result.Error);
    Assert.False(Directory.Exists(HiddenOf(Location, "Game")));
    Assert.False(Directory.Exists(Path.Combine(Location, "Game")));
    Assert.Single(_log.Warnings);
  }

  [Fact]
  public void AFailedCleanupKeepsTheOriginalError()
  {
    var files = new FailingFileSystem { FailMove = true, FailDelete = true };

    var result = NewCreator(files).Create(MakeTemplate(), Location, "Game");

    Assert.Contains("You do not have permission to use that folder or the template.", result.Error);
    Assert.DoesNotContain("delete failed", result.Error);
    Assert.Contains("move to x is denied", _log.Warnings[0]);
    Assert.Equal(2, _log.Warnings.Count);
    Assert.Contains(ProjectCreator.HiddenFolderName("Game"), _log.Warnings[1]);
  }

  [Fact]
  public void CheckTargetClassifiesThePath()
  {
    var creator = NewCreator();
    Directory.CreateDirectory(Location);
    Assert.Equal(ProjectTargetState.Free, creator.CheckTarget(Location, "Absent"));

    Directory.CreateDirectory(Path.Combine(Location, "Empty"));
    Assert.Equal(ProjectTargetState.Free, creator.CheckTarget(Location, "Empty"));

    Directory.CreateDirectory(Path.Combine(Location, "WithFile"));
    File.WriteAllText(Path.Combine(Location, "WithFile", "a.txt"), "a");
    Assert.Equal(ProjectTargetState.NotEmpty, creator.CheckTarget(Location, "WithFile"));

    Directory.CreateDirectory(Path.Combine(Location, "WithFolder", "sub"));
    Assert.Equal(ProjectTargetState.NotEmpty, creator.CheckTarget(Location, "WithFolder"));

    File.WriteAllText(Path.Combine(Location, "AFile"), "a");
    Assert.Equal(ProjectTargetState.NotAFolder, creator.CheckTarget(Location, "AFile"));
  }

  [Fact]
  public void PhysicalEnumerateFilesIsSortedAndEmptyWhenMissing()
  {
    var files = new PhysicalFileSystem();
    Directory.CreateDirectory(_root);
    File.WriteAllText(Path.Combine(_root, "b.txt"), "b");
    File.WriteAllText(Path.Combine(_root, "a.txt"), "a");
    Directory.CreateDirectory(Path.Combine(_root, "sub"));

    Assert.Equal(
      [Path.Combine(_root, "a.txt"), Path.Combine(_root, "b.txt")],
      files.EnumerateFiles(_root));
    Assert.Empty(files.EnumerateFiles(Path.Combine(_root, "missing")));
  }

  [Fact]
  public void PhysicalCopyFileDoesNotOverwrite()
  {
    var files = new PhysicalFileSystem();
    Directory.CreateDirectory(_root);
    var source = Path.Combine(_root, "s.txt");
    var destination = Path.Combine(_root, "d.txt");
    File.WriteAllText(source, "new");
    files.CopyFile(source, Path.Combine(_root, "fresh.txt"));
    Assert.Equal("new", File.ReadAllText(Path.Combine(_root, "fresh.txt")));

    File.WriteAllText(destination, "old");
    Assert.Throws<IOException>(() => files.CopyFile(source, destination));
    Assert.Equal("old", File.ReadAllText(destination));
  }

  [Theory]
  [InlineData("")]
  [InlineData("   ")]
  [InlineData("a\0b")]
  public void ABadLocationIsAnErrorNotAnException(string location)
  {
    var creator = NewCreator();

    var result = creator.Create(MakeTemplate(), location, "Game");

    Assert.False(result.Succeeded);
    Assert.False(string.IsNullOrEmpty(result.Error));
    Assert.Equal(ProjectTargetState.Unavailable, creator.CheckTarget(location, "Game"));
    Assert.False(Directory.Exists(Path.Combine(Directory.GetCurrentDirectory(), "Game")));
    Assert.Equal([Path.Combine(_root, "template")], Directory.GetDirectories(_root));
  }

  [Fact]
  public void AnUnreadableTargetIsUnavailable()
  {
    Directory.CreateDirectory(Path.Combine(Location, "Game"));
    var failing = new FailingFileSystem { FailEnumerateFiles = true };

    Assert.Equal(ProjectTargetState.Unavailable, NewCreator(failing).CheckTarget(Location, "Game"));
    Assert.Equal(ProjectTargetState.Free, NewCreator(new FailingFileSystem()).CheckTarget(Location, "Game"));

    var result = NewCreator(failing).Create(MakeTemplate(), Location, "Game");
    Assert.False(result.Succeeded);
    Assert.False(File.Exists(Path.Combine(Location, "Game", "project.ecs3d")));
  }

  [Fact]
  public void OutputKeepsCharactersLiteral()
  {
    var name = "Caf\u00E9 <A+B> & 'q'";
    var json = "{\"assets\":{\"scenes\":[{\"uuid\":\"s\",\"objects\":[{\"name\":\"" + name + "\"}]}]}}";

    var output = ProjectCreator.RewriteObjectUuids(json, NextUuid);

    Assert.Contains("\"name\": \"" + name + "\"", output);
  }

  [Fact]
  public void AReadOnlyProjectFileIsCopiedEditable()
  {
    var template = MakeTemplate();
    File.SetAttributes(Path.Combine(template.FolderPath, "project.ecs3d"), FileAttributes.ReadOnly);

    var result = NewCreator().Create(template, Location, "Game");

    Assert.True(result.Succeeded);
    var copied = Path.Combine(result.ProjectFolder!, "project.ecs3d");
    Assert.False(File.GetAttributes(copied).HasFlag(FileAttributes.ReadOnly));
    Assert.DoesNotContain("obj-1", File.ReadAllText(copied));
  }

  [Fact]
  public void AFailedStaleCleanupIsAnErrorWithNoTarget()
  {
    var template = MakeTemplate();
    Directory.CreateDirectory(HiddenOf(Location, "Game"));

    var result = NewCreator(new FailingFileSystem { FailDelete = true }).Create(template, Location, "Game");

    Assert.False(result.Succeeded);
    Assert.Contains("delete failed", result.Error);
    Assert.False(Directory.Exists(Path.Combine(Location, "Game")));
  }
}
