using System;
using System.Collections.Generic;
using System.Globalization;

namespace ECS3DLauncher.Services;

// A dotted numeric version with an optional "-suffix" pre-release, or the "dev" build that outranks every release.
public sealed class EngineVersion
{
    private const string DevName = "dev";

    private readonly IReadOnlyList<int> _parts;
    private readonly string _preRelease;
    private readonly bool _isDev;

    private EngineVersion(IReadOnlyList<int> parts, string preRelease, bool isDev)
    {
        _parts = parts;
        _preRelease = preRelease;
        _isDev = isDev;
    }

    public static EngineVersion Dev { get; } = new([], "", true);

    // "1.2", "1.2.3-beta" and "dev" parse; a "+build" suffix is dropped. Anything else is not a version.
    public static bool TryParse(string? text, out EngineVersion version)
    {
        version = Dev;
        var trimmed = text?.Trim() ?? "";
        if (trimmed.Equals(DevName, StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        var plus = trimmed.IndexOf('+');
        var withoutBuild = plus >= 0 ? trimmed[..plus] : trimmed;
        var dash = withoutBuild.IndexOf('-');
        var core = dash >= 0 ? withoutBuild[..dash] : withoutBuild;
        var preRelease = dash >= 0 ? withoutBuild[(dash + 1)..] : "";
        if (dash >= 0 && preRelease.Length == 0)
        {
            return false;
        }

        var parts = new List<int>();
        foreach (var piece in core.Split('.'))
        {
            if (!int.TryParse(piece, NumberStyles.None, null, out var number))
            {
                return false;
            }

            parts.Add(number);
        }

        version = new EngineVersion(parts, preRelease, false);
        return true;
    }

    // Missing trailing parts count as 0, and a pre-release sorts before its release.
    public static int Compare(EngineVersion left, EngineVersion right)
    {
        if (left._isDev || right._isDev)
        {
            return left._isDev.CompareTo(right._isDev);
        }

        var count = Math.Max(left._parts.Count, right._parts.Count);
        for (var i = 0; i < count; i++)
        {
            var difference = left.PartAt(i).CompareTo(right.PartAt(i));
            if (difference != 0)
            {
                return difference;
            }
        }

        return ComparePreRelease(left._preRelease, right._preRelease);
    }

    private int PartAt(int index) => index < _parts.Count ? _parts[index] : 0;

    private static int ComparePreRelease(string left, string right)
    {
        if (left.Length == 0 || right.Length == 0)
        {
            return (left.Length == 0).CompareTo(right.Length == 0);
        }

        // Two pre-releases compare ordinally; only "pre-release < release" is relied on.
        return Math.Sign(string.CompareOrdinal(left, right));
    }
}

public static class TemplateCompatibility
{
    // A launcher version that is not a version at all is trusted rather than locking every template out.
    public static bool NeedsNewerEngine(string minEngineVersion, string launcherVersion)
    {
        if (!EngineVersion.TryParse(minEngineVersion, out var required)
            || !EngineVersion.TryParse(launcherVersion, out var current))
        {
            return false;
        }

        return EngineVersion.Compare(required, current) > 0;
    }
}
