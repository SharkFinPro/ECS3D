using System;
using System.Linq;

namespace ECS3DLauncher.Services;

public static class SearchMatch
{
    // An empty query matches everything; otherwise any field containing it, ignoring case, matches.
    public static bool Matches(string? query, params string[] fields)
    {
        if (string.IsNullOrWhiteSpace(query))
        {
            return true;
        }

        var trimmed = query.Trim();
        return fields.Any(field => field.Contains(trimmed, StringComparison.OrdinalIgnoreCase));
    }
}
