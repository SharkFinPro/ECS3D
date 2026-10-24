using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace ECS3DLauncher.Services;

public enum ProjectTargetState { Free, NotEmpty, NotAFolder }

public sealed record ProjectCreationResult(string? ProjectFolder, string? Error)
{
    public bool Succeeded => ProjectFolder is not null;
}

// Copies a template into <location>/<name> through a hidden sibling, so the final rename is the commit.
public sealed class ProjectCreator
{
    private static readonly JsonSerializerOptions WriteOptions = new() { WriteIndented = true, IndentSize = 2 };

    private readonly IFileSystem _files;
    private readonly ILauncherLog _log;
    private readonly Func<Guid> _newUuid;

    public ProjectCreator(IFileSystem files, ILauncherLog log) : this(files, log, Guid.NewGuid)
    {
    }

    public ProjectCreator(IFileSystem files, ILauncherLog log, Func<Guid> newUuid)
    {
        _files = files;
        _log = log;
        _newUuid = newUuid;
    }

    public static string HiddenFolderName(string name) => $".{name}.creating";

    // Free when <location>/<name> does not exist or is an empty folder; NotAFolder when a file has that path.
    public ProjectTargetState CheckTarget(string location, string name)
    {
        var target = Path.Combine(location, name);
        if (_files.FileExists(target))
        {
            return ProjectTargetState.NotAFolder;
        }

        if (!_files.DirectoryExists(target))
        {
            return ProjectTargetState.Free;
        }

        var empty = _files.EnumerateFiles(target).Count == 0 && _files.EnumerateDirectories(target).Count == 0;
        return empty ? ProjectTargetState.Free : ProjectTargetState.NotEmpty;
    }

    public ProjectCreationResult Create(TemplateEntry template, string location, string name)
    {
        var invalid = ProjectNameRules.Validate(name);
        if (invalid is not null)
        {
            return new ProjectCreationResult(null, invalid);
        }

        var target = Path.Combine(location, name);
        var hidden = Path.Combine(location, HiddenFolderName(name));
        try
        {
            _files.CreateDirectory(location);
            var blocked = TargetError(location, name, target);
            if (blocked is not null)
            {
                return new ProjectCreationResult(null, blocked);
            }

            Build(template, hidden, target);
            return new ProjectCreationResult(target, null);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException or InvalidDataException)
        {
            _log.Warn($"Creating project {target} failed: {ex.Message}");
            RemoveHidden(hidden);
            return new ProjectCreationResult(null, $"Could not create the project: {ex.Message}");
        }
    }

    internal static string RewriteObjectUuids(string projectJson, Func<Guid> newUuid)
    {
        var root = JsonNode.Parse(projectJson);
        if (root is not JsonObject project || project["assets"]?["scenes"] is not JsonArray scenes)
        {
            throw new InvalidDataException("project.ecs3d has no scenes list.");
        }

        foreach (var scene in scenes.OfType<JsonObject>())
        {
            RewriteObjects(scene["objects"], newUuid);
        }

        return project.ToJsonString(WriteOptions);
    }

    private static void RewriteObjects(JsonNode? list, Func<Guid> newUuid)
    {
        if (list is not JsonArray objects)
        {
            return;
        }

        foreach (var item in objects.OfType<JsonObject>())
        {
            item["uuid"] = newUuid().ToString("D");
            RewriteObjects(item["children"], newUuid);
        }
    }

    private string? TargetError(string location, string name, string target) =>
        CheckTarget(location, name) switch
        {
            ProjectTargetState.NotEmpty => $"{target} already exists and is not empty.",
            ProjectTargetState.NotAFolder => $"{target} already exists as a file.",
            _ => null
        };

    private void Build(TemplateEntry template, string hidden, string target)
    {
        if (_files.DirectoryExists(hidden))
        {
            _files.DeleteDirectory(hidden, true);
        }

        CopyTree(template.FolderPath, hidden, true);
        var projectFile = Path.Combine(hidden, RecentProjectPaths.ProjectFileName);
        if (!_files.FileExists(projectFile))
        {
            throw new InvalidDataException("The template has no project.ecs3d.");
        }

        _files.WriteAllTextAtomic(projectFile, RewriteObjectUuids(_files.ReadAllText(projectFile), _newUuid));
        if (_files.DirectoryExists(target))
        {
            _files.DeleteDirectory(target, false);
        }

        _files.MoveDirectory(hidden, target);
    }

    private void CopyTree(string source, string destination, bool top)
    {
        _files.CreateDirectory(destination);
        foreach (var file in _files.EnumerateFiles(source))
        {
            var fileName = Path.GetFileName(file);
            if (!(top && fileName == TemplateCatalog.ManifestFileName))
            {
                _files.CopyFile(file, Path.Combine(destination, fileName));
            }
        }

        foreach (var folder in _files.EnumerateDirectories(source))
        {
            CopyTree(folder, Path.Combine(destination, Path.GetFileName(folder)), false);
        }
    }

    private void RemoveHidden(string hidden)
    {
        try
        {
            if (_files.DirectoryExists(hidden))
            {
                _files.DeleteDirectory(hidden, true);
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            _log.Warn($"Could not remove the unfinished project folder {hidden}: {ex.Message}");
        }
    }
}
