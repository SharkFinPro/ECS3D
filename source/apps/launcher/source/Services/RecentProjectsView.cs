using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;

namespace ECS3DLauncher.Services;

// What the Projects tab shows of the stored list: order, search, missing check and the relative time.
public static class RecentProjectsView
{
    // Pinned first, then newest first; an unparseable time sorts as the oldest.
    public static IReadOnlyList<RecentProject> Sort(IEnumerable<RecentProject> entries) =>
        entries
            .OrderByDescending(entry => entry.Pinned)
            .ThenByDescending(entry => RecentProjectTime.Parse(entry.LastOpenedUtc) ?? DateTimeOffset.MinValue)
            .ToList();

    // Matches the name or the path, ignoring case; a blank query keeps everything.
    public static IReadOnlyList<RecentProject> Filter(IEnumerable<RecentProject> entries, string? query)
    {
        return entries.Where(entry => SearchMatch.Matches(query, entry.Name, entry.Path)).ToList();
    }

    // A folder without project.ecs3d counts as missing, as does a legacy file that is gone.
    public static bool IsMissing(IFileSystem fileSystem, RecentProject entry)
    {
        if (string.IsNullOrWhiteSpace(entry.Path))
        {
            return true;
        }

        if (fileSystem.DirectoryExists(entry.Path))
        {
            return !fileSystem.FileExists(RecentProjectPaths.ProjectFileIn(entry.Path));
        }

        return !(RecentProjectPaths.IsLegacyFile(entry.Path) && fileSystem.FileExists(entry.Path));
    }

    // Relative for the first week, then the date; the date is in UTC like the stored time.
    public static string EditedText(string lastOpenedUtc, DateTimeOffset now)
    {
        if (RecentProjectTime.Parse(lastOpenedUtc) is not { } opened)
        {
            return "Unknown";
        }

        // A time in the future (clock skew between machines) reads as just now.
        var elapsed = now - opened;
        if (elapsed < TimeSpan.FromMinutes(1))
        {
            return "just now";
        }

        if (elapsed < TimeSpan.FromHours(1))
        {
            return Count((int)elapsed.TotalMinutes, "minute");
        }

        if (elapsed < TimeSpan.FromDays(1))
        {
            return Count((int)elapsed.TotalHours, "hour");
        }

        if (elapsed < TimeSpan.FromDays(2))
        {
            return "Yesterday";
        }

        return elapsed < TimeSpan.FromDays(7)
            ? Count((int)elapsed.TotalDays, "day")
            : opened.ToString("MMM d, yyyy", CultureInfo.InvariantCulture);
    }

    private static string Count(int amount, string unit) => $"{amount} {unit}{(amount == 1 ? "" : "s")} ago";
}
