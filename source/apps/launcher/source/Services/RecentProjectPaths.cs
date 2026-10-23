using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace ECS3DLauncher.Services;

// Mirrors canonicalPath/displayName in the engine's source/libs/settings/RecentProjects.cpp.
internal static class RecentProjectPaths
{
    public const string ProjectFileName = "project.ecs3d";

    private const string ProjectExtension = ".ecs3d";
    private const string LegacyExtension = ".json";

    // Absolute, forward slashes, no trailing slash except on a root; any .ecs3d file becomes its folder.
    public static string Normalize(string path, HostOs os)
    {
        if (string.IsNullOrEmpty(path))
        {
            return string.Empty;
        }

        var text = os == HostOs.Windows ? path.Replace('\\', '/') : path;
        if (!IsAbsolute(text, os))
        {
            text = MakeAbsolute(text, os);
        }

        var (root, rest) = SplitRoot(text, os);
        var segments = Collapse(rest);

        // A trailing separator leaves the C++ path with an empty filename, so a ".ecs3d" folder name stays.
        if (!text.EndsWith('/') && segments.Count > 0 && HasExtension(segments[^1], ProjectExtension))
        {
            segments.RemoveAt(segments.Count - 1);
        }

        return root + string.Join('/', segments);
    }

    // Case-insensitive (ASCII) on Windows and macOS, case-sensitive on Linux.
    public static string Key(string path, HostOs os)
    {
        var normalized = Normalize(path, os);
        return os == HostOs.Linux ? normalized : AsciiLower(normalized);
    }

    // The folder name, or the file name without extension for a legacy .json file.
    public static string DisplayName(string normalized)
    {
        var name = normalized[(normalized.LastIndexOf('/') + 1)..];
        return HasExtension(name, LegacyExtension) ? name[..^LegacyExtension.Length] : name;
    }

    // Judges the last segment only, so a folder named Foo.json inside another path is not a legacy file.
    public static bool IsLegacyFile(string path) =>
        HasExtension(path[(path.LastIndexOfAny(['/', '\\']) + 1)..], LegacyExtension);

    // Accepts either separator, as a folder from the file dialog may use backslashes.
    public static string ProjectFileIn(string folder) => folder.TrimEnd('/', '\\') + "/" + ProjectFileName;

    private static bool HasExtension(string name, string extension) =>
        name.Length > extension.Length && name.EndsWith(extension, StringComparison.OrdinalIgnoreCase);

    private static bool IsAbsolute(string text, HostOs os)
    {
        if (os != HostOs.Windows)
        {
            return text.StartsWith('/');
        }

        var drive = text.Length >= 3 && char.IsAsciiLetter(text[0]) && text[1] == ':' && text[2] == '/';
        return drive || text.StartsWith("//", StringComparison.Ordinal);
    }

    private static string MakeAbsolute(string text, HostOs os)
    {
        try
        {
            text = Path.GetFullPath(text);
        }
        catch (Exception ex) when (ex is ArgumentException or NotSupportedException or PathTooLongException)
        {
            // A path the OS rejects stays as written; it is still a stable key.
            return text;
        }

        return os == HostOs.Windows ? text.Replace('\\', '/') : text;
    }

    private static (string Root, string Remainder) SplitRoot(string text, HostOs os)
    {
        if (text.Length >= 3 && char.IsAsciiLetter(text[0]) && text[1] == ':' && text[2] == '/')
        {
            return (text[..3], text[3..]);
        }

        if (os == HostOs.Windows && text.StartsWith("//", StringComparison.Ordinal))
        {
            // The server and share form the root, so ".." cannot climb past them.
            var parts = text[2..].Split('/', StringSplitOptions.RemoveEmptyEntries);
            var share = parts.Take(2).ToArray();
            var root = "//" + string.Join('/', share) + (share.Length > 0 ? "/" : string.Empty);
            return (root, string.Join('/', parts.Skip(2)));
        }

        return text.StartsWith('/') ? ("/", text[1..]) : (string.Empty, text);
    }

    private static List<string> Collapse(string rest)
    {
        var segments = new List<string>();
        foreach (var part in rest.Split('/'))
        {
            if (part.Length == 0 || part == ".")
            {
                continue;
            }

            if (part == "..")
            {
                if (segments.Count > 0)
                {
                    segments.RemoveAt(segments.Count - 1);
                }

                continue;
            }

            segments.Add(part);
        }

        return segments;
    }

    private static string AsciiLower(string text)
    {
        var chars = text.ToCharArray();
        for (var i = 0; i < chars.Length; i++)
        {
            if (chars[i] is >= 'A' and <= 'Z')
            {
                chars[i] = (char)(chars[i] - 'A' + 'a');
            }
        }

        return new string(chars);
    }
}
