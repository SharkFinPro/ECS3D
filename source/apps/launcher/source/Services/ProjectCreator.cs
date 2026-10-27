using System;
using System.IO;
using System.Linq;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading;

namespace ECS3DLauncher.Services;

public enum ProjectTargetState { Free, NotEmpty, NotAFolder, Unavailable, Unreachable }

public sealed record ProjectCreationResult(string? ProjectFolder, string? Error)
{
    public bool Succeeded => ProjectFolder is not null;
}

// Copies a template into <location>/<name> through a hidden sibling, so the final rename is the commit.
public sealed class ProjectCreator
{
    public static string CanceledMessage => "Canceled.";

    public static string UnreachableMessage =>
        "This location cannot be reached. Check that the drive or network share is available.";

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

    // Free when the location is a folder or can be made into one; NotAFolder when a file is in the way; Unreachable
    // when its drive or network share does not exist. A relative path has no root to check. Never throws.
    public ProjectTargetState CheckLocation(string location)
    {
        if (!IsUsablePath(location))
        {
            return ProjectTargetState.Unavailable;
        }

        try
        {
            var root = Path.GetPathRoot(location);
            if (!string.IsNullOrEmpty(root) && !_files.DirectoryExists(root))
            {
                return ProjectTargetState.Unreachable;
            }

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
    public ProjectCreationResult Create(TemplateEntry template, string location, string name) =>
        Create(template, location, name, CancellationToken.None);

    public ProjectCreationResult Create(
        TemplateEntry template,
        string location,
        string name,
        CancellationToken cancel)
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

        if (CheckLocation(location) == ProjectTargetState.Unreachable)
        {
            return new ProjectCreationResult(null, UnreachableMessage);
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
            var unreachable = ex is IOException && CheckLocation(location) == ProjectTargetState.Unreachable;
            return new ProjectCreationResult(null, unreachable ? UnreachableMessage : DescribeFailure(ex, location));
        }
    }

    // Plain words for a failed copy; the raw exception text belongs in the log.
    public static string DescribeFailure(Exception ex, string location) => ex switch
    {
        UnauthorizedAccessException =>
            $"Could not create the project in {location}. " +
            "You do not have permission to use that folder or the template.",
        JsonException => "Could not create the project: the template's project file is not valid.",
        InvalidDataException => $"Could not create the project: {ex.Message}",
        _ => $"Could not create the project in {location}: {ShortReason(ex)}"
    };

    // The first line of a .NET message without the quoted path some of them append after " : '".
    internal static string ShortReason(Exception ex)
    {
        var line = ex.Message.Split('\n')[0].Trim();
        var quoted = line.IndexOf(" : '", StringComparison.Ordinal);
        var reason = quoted > 0 ? line[..quoted].Trim() : line;
        return reason.Length > 0 ? reason : ex.GetType().Name;
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
