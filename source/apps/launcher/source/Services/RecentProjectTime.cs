using System;
using System.Globalization;

namespace ECS3DLauncher.Services;

// The lastOpenedUtc format of source/libs/settings/RecentProjects.md: whole seconds, a Z, nothing else.
internal static class RecentProjectTime
{
    private const string Format = "yyyy-MM-dd'T'HH:mm:ss'Z'";
    private const string Shape = "dddd-dd-ddTdd:dd:ddZ";

    public static string Write(DateTimeOffset time) =>
        time.UtcDateTime.ToString(Format, CultureInfo.InvariantCulture);

    // The exact written format parses; anything else is null.
    public static DateTimeOffset? Parse(string? text)
    {
        if (text is null || !MatchesShape(text))
        {
            return null;
        }

        var parsed = DateTimeOffset.TryParseExact(
            text,
            Format,
            CultureInfo.InvariantCulture,
            DateTimeStyles.AssumeUniversal | DateTimeStyles.AdjustToUniversal,
            out var time);
        return parsed ? time : null;
    }

    private static bool MatchesShape(string text)
    {
        if (text.Length != Shape.Length)
        {
            return false;
        }

        for (var i = 0; i < Shape.Length; i++)
        {
            var matches = Shape[i] == 'd' ? text[i] is >= '0' and <= '9' : text[i] == Shape[i];
            if (!matches)
            {
                return false;
            }
        }

        return true;
    }
}
