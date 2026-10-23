using System;

namespace ECS3DLauncher.Services;

public enum ProjectPathType
{
    Invalid,
    Folder,
    ProjectFile,
    LegacyFile,
}

// Path is what to record in the recent list; Error is the dialog's message when the type is Invalid.
public sealed record ProjectPathResult(ProjectPathType Type, string Path, string? Error);

// Judges a choice in Open existing...: a project folder, its project.ecs3d, or a legacy .json project.
public static class ProjectPathKind
{
    public static ProjectPathResult Classify(IFileSystem fileSystem, string path)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            return Invalid("Choose a project folder or a project file.");
        }

        if (fileSystem.DirectoryExists(path))
        {
            return fileSystem.FileExists(RecentProjectPaths.ProjectFileIn(path))
                ? new ProjectPathResult(ProjectPathType.Folder, path, null)
                : Invalid($"That folder has no {RecentProjectPaths.ProjectFileName} file, so it is not a project.");
        }

        if (!fileSystem.FileExists(path))
        {
            return Invalid("That path does not exist.");
        }

        var name = path[(path.LastIndexOfAny(['/', '\\']) + 1)..];
        if (string.Equals(name, RecentProjectPaths.ProjectFileName, StringComparison.Ordinal))
        {
            return new ProjectPathResult(ProjectPathType.ProjectFile, FolderOf(path, name), null);
        }

        return RecentProjectPaths.IsLegacyFile(name)
            ? new ProjectPathResult(ProjectPathType.LegacyFile, path, null)
            : Invalid($"Choose a project folder, a {RecentProjectPaths.ProjectFileName} file or a .json project file.");
    }

    // A root keeps its separator; a bare file name is in the current folder.
    private static string FolderOf(string path, string fileName)
    {
        var withSeparator = path[..^fileName.Length];
        if (withSeparator.Length == 0)
        {
            return ".";
        }

        var trimmed = withSeparator.TrimEnd('/', '\\');
        return trimmed.Length == 0 || trimmed.EndsWith(':') ? withSeparator : trimmed;
    }

    private static ProjectPathResult Invalid(string message) =>
        new(ProjectPathType.Invalid, string.Empty, message);
}
