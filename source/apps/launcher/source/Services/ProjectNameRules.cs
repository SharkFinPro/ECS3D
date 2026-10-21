using System;
using System.Linq;

namespace ECS3DLauncher.Services;

// A project name becomes a folder name, so it must be valid on Windows, macOS and Linux alike.
public static class ProjectNameRules
{
    public const int MaxLength = 64;

    private const string InvalidCharacters = "<>:\"/\\|?*";

    private const int MaxSuffix = 100000;

    private static readonly string[] ReservedNames =
        ["CON", "PRN", "AUX", "NUL", .. Enumerable.Range(1, 9).SelectMany(n => new[] { $"COM{n}", $"LPT{n}" })];

    // Null when the name is acceptable; otherwise the message for the first rule it breaks.
    public static string? Validate(string? name)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            return "Enter a project name.";
        }

        if (name.Length > MaxLength)
        {
            return $"Use at most {MaxLength} characters.";
        }

        if (name.Any(c => char.IsControl(c) || InvalidCharacters.Contains(c)))
        {
            return "The name cannot contain < > : \" / \\ | ? * or control characters.";
        }

        if (name[0] is ' ' or '.' || name[^1] is ' ' or '.')
        {
            return "The name cannot start or end with a space or a dot.";
        }

        return IsReserved(name) ? $"\"{name}\" is a reserved name on Windows." : null;
    }

    // First-Person, then First-Person 2, 3... as the editor's default save folder does.
    public static string UniqueDefault(string baseName, Func<string, bool> isTaken)
    {
        var candidate = baseName;
        for (var suffix = 2; isTaken(candidate) && suffix < MaxSuffix; suffix++)
        {
            candidate = $"{baseName} {suffix}";
        }

        return candidate;
    }

    // Windows reserves these with any extension too, so "con.txt" counts.
    private static bool IsReserved(string name)
    {
        var dot = name.IndexOf('.');
        var stem = (dot >= 0 ? name[..dot] : name).TrimEnd(' ');
        return ReservedNames.Contains(stem, StringComparer.OrdinalIgnoreCase);
    }
}
