using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace ECS3DLauncher.Services;

public sealed record TemplateEntry(TemplateManifest Manifest, string FolderPath, string? ThumbnailPath);

// FolderFound tells a missing templates folder from an empty one, so the UI can say which.
public sealed record TemplateCatalogContents(
    string TemplatesFolder,
    bool FolderFound,
    IReadOnlyList<TemplateEntry> Templates,
    IReadOnlyList<TemplateEntry> Samples)
{
    // True when the templates folder exists but listing it failed.
    public bool Unreadable { get; init; }

    public bool IsEmpty => Templates.Count == 0 && Samples.Count == 0;
}

public sealed class TemplateCatalog
{
    public static string ManifestFileName => "template.json";

    private readonly IFileSystem _files;
    private readonly ILauncherLog _log;

    public TemplateCatalog(IFileSystem files, ILauncherLog log)
    {
        _files = files;
        _log = log;
    }

    // Reads <engine>/templates/*/template.json. A folder with an unusable manifest is skipped with one warning.
    public TemplateCatalogContents Load(string engineFolder)
    {
        var root = Path.Combine(engineFolder, "templates");
        if (!_files.DirectoryExists(root))
        {
            return new TemplateCatalogContents(root, false, [], []);
        }

        IReadOnlyList<string> folders;
        try
        {
            folders = _files.EnumerateDirectories(root);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            _log.Warn($"Could not read the templates folder {root}: {ex.Message}");
            return new TemplateCatalogContents(root, true, [], []) { Unreadable = true };
        }

        var entries = new List<TemplateEntry>();
        var ids = new HashSet<string>(StringComparer.Ordinal);
        foreach (var folder in folders)
        {
            var entry = ReadFolder(folder);
            if (entry is null)
            {
                continue;
            }

            if (ids.Add(entry.Manifest.Id))
            {
                entries.Add(entry);
            }
            else
            {
                Skip(folder, $"id '{entry.Manifest.Id}' is already used by an earlier folder");
            }
        }

        return new TemplateCatalogContents(root, true,
            OfCategory(entries, TemplateManifest.TemplateCategory), OfCategory(entries, TemplateManifest.SampleCategory));
    }

    private static List<TemplateEntry> OfCategory(List<TemplateEntry> entries, string category) =>
        entries
            .Where(entry => entry.Manifest.Category == category)
            .OrderBy(entry => entry.Manifest.Name, StringComparer.OrdinalIgnoreCase)
            .ThenBy(entry => entry.Manifest.Id, StringComparer.Ordinal)
            .ToList();

    private TemplateEntry? ReadFolder(string folder)
    {
        var manifestPath = Path.Combine(folder, ManifestFileName);
        if (!_files.FileExists(manifestPath))
        {
            Skip(folder, $"{ManifestFileName} not found");
            return null;
        }

        string json;
        try
        {
            json = _files.ReadAllText(manifestPath);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            Skip(folder, $"{ManifestFileName} could not be read ({ex.Message})");
            return null;
        }

        if (!TemplateManifestParser.TryParse(json, out var manifest, out var reason))
        {
            Skip(folder, reason);
            return null;
        }

        return new TemplateEntry(manifest!, folder, ThumbnailPathOf(folder, manifest!.Thumbnail));
    }

    private string? ThumbnailPathOf(string folder, string thumbnail)
    {
        var escapes = Path.IsPathRooted(thumbnail) || thumbnail.Split(['/', '\\']).Contains("..");
        var path = Path.Combine(folder, thumbnail);
        return !escapes && thumbnail.Length > 0 && _files.FileExists(path) ? path : null;
    }

    private void Skip(string folder, string reason) => _log.Warn($"Skipped template folder {folder}: {reason}");
}
