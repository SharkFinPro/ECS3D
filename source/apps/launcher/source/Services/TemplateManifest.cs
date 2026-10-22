using System.Collections.Generic;
using System.Globalization;

namespace ECS3DLauncher.Services;

public sealed record TemplatePlayers(int Min, int Max);

// The fields of a template.json; source/templates/README.md is the schema.
public sealed record TemplateManifest(
    string Id,
    string Name,
    string Description,
    string Category,
    IReadOnlyList<string> Tags,
    string Thumbnail,
    string EntryScene,
    string MinEngineVersion)
{
    public TemplatePlayers? Players { get; init; }

    public static string TemplateCategory => "template";

    public static string SampleCategory => "sample";

    public bool IsMultiplayer => Players is { Max: > 1 };

    // "1-4 players" for a multiplayer entry; null for a single-player one.
    public string? PlayersLabel =>
        Players is { Max: > 1 } players
            ? string.Create(CultureInfo.InvariantCulture, $"{players.Min}-{players.Max} players")
            : null;
}
