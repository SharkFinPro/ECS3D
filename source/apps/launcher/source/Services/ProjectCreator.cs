using System;
using System.IO;
using System.Linq;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading;

namespace ECS3DLauncher.Services;

public enum ProjectTargetState { Free, NotEmpty, NotAFolder, Unavailable }

public sealed record ProjectCreationResult(string? ProjectFolder, string? Error)
{
    public bool Succeeded => ProjectFolder is not null;
}

// Copies a template into <location>/<name> through a hidden sibling, so the final rename is the commit.
public sealed class ProjectCreator
{
    public const string CanceledMessage = "Canceled.";

    // Only the engine's JSON parser reads this file, never a web page, so characters stay literal.
    private static readonly JsonSerializerOptions WriteOptions = new()
    {
        WriteIndented = true,
        IndentSize = 2,
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping
    };

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

    // A missing or empty folder is Free, a file at that path is NotAFolder, and a malformed or unreadable path is
    // Unavailable. Never throws.
    public ProjectTargetState CheckTarget(string location, string name)
    {
        if (!IsUsablePath(location) || !IsUsablePath(name))
        {
            return ProjectTargetState.Unavailable;
        }

        try
        {
            return Classify(Path.Combine(location, name));
        }
        catch (Exception ex) when (IsExpected(ex))
        {
            return ProjectTargetState.Unavailable;
        }
    }

    // Free when the location is a folder or can be made into one; NotAFolder when a file is in the way.
    public ProjectTargetState CheckLocation(string location)
    {
        if (!IsUsablePath(location))
        {
            return ProjectTargetState.Unavailable;
        }

        try
        {
            return _files.FileExists(location) ? ProjectTargetState.NotAFolder : ProjectTargetState.Free;
        }
        catch (Exception ex) when (IsExpected(ex))
        {
            return ProjectTargetState.Unavailable;
        }
    }

    private ProjectTargetState Classify(string target)
    {
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

    // Cancelling is checked between files and before the commit; a cancelled copy removes the hidden folder as a
    // failed one does and reports Canceled.
    public ProjectCreationResult Create(
        TemplateEntry template,
        string location,
        string name,
        CancellationToken cancel = default)
    {
        var invalid = ProjectNameRules.Validate(name);
        if (invalid is not null)
        {
            return new ProjectCreationResult(null, invalid);
        }

        if (!IsUsablePath(location))
        {
            return new ProjectCreationResult(null, "The location is not a valid folder path.");
        }

        // Both parts are validated above, so combining them cannot throw.
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

            Build(template, hidden, target, cancel);
            return new ProjectCreationResult(target, null);
        }
        catch (OperationCanceledException) when (cancel.IsCancellationRequested)
        {
            RemoveHidden(hidden);
            return new ProjectCreationResult(null, CanceledMessage);
        }
        catch (Exception ex) when (IsExpected(ex))
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

    private static bool IsUsablePath(string? path) => !string.IsNullOrWhiteSpace(path) && !path.Contains('\0');

    private static bool IsExpected(Exception ex) =>
        ex is IOException or UnauthorizedAccessException or JsonException or InvalidDataException
            or ArgumentException or NotSupportedException;

    private string? TargetError(string location, string name, string target) =>
        CheckTarget(location, name) switch
        {
            ProjectTargetState.NotEmpty => $"{target} already exists and is not empty.",
            ProjectTargetState.NotAFolder => $"{target} already exists as a file.",
            ProjectTargetState.Unavailable => $"{target} cannot be used as a project folder.",
            _ => null
        };

    private void Build(TemplateEntry template, string hidden, string target, CancellationToken cancel)
    {
        if (_files.DirectoryExists(hidden))
        {
            _files.DeleteDirectory(hidden, true);
        }

        CopyTree(template.FolderPath, hidden, true, cancel);
        var projectFile = Path.Combine(hidden, RecentProjectPaths.ProjectFileName);
        if (!_files.FileExists(projectFile))
        {
            throw new InvalidDataException("The template has no project.ecs3d.");
        }

        cancel.ThrowIfCancellationRequested();
        _files.WriteAllTextAtomic(projectFile, RewriteObjectUuids(_files.ReadAllText(projectFile), _newUuid));
        cancel.ThrowIfCancellationRequested();
        if (_files.DirectoryExists(target))
        {
            _files.DeleteDirectory(target, false);
        }

        _files.MoveDirectory(hidden, target);
    }

    private void CopyTree(string source, string destination, bool top, CancellationToken cancel)
    {
        _files.CreateDirectory(destination);
        foreach (var file in _files.EnumerateFiles(source))
        {
            cancel.ThrowIfCancellationRequested();
            var fileName = Path.GetFileName(file);
            if (!(top && fileName == TemplateCatalog.ManifestFileName))
            {
                _files.CopyFile(file, Path.Combine(destination, fileName));
            }
        }

        foreach (var folder in _files.EnumerateDirectories(source))
        {
            CopyTree(folder, Path.Combine(destination, Path.GetFileName(folder)), false, cancel);
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
