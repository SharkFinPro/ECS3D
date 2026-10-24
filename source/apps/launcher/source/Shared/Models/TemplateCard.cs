using System.Collections.Generic;
using Avalonia.Media.Imaging;

namespace ECS3DLauncher.Shared.Models;

// A project template card: the manifest fields the Templates tab shows, plus the shipped folder a new project copies.
public sealed class TemplateCard
{
    public required string Id { get; init; }
    public required string Name { get; init; }
    public required string Description { get; init; }
    public required IReadOnlyList<string> Tags { get; init; }
    public required string FolderPath { get; init; }

    // "1-4 players" for a multiplayer template; null for a single-player one.
    public string? PlayersLabel { get; init; }
    public bool HasPlayers => PlayersLabel is not null;

    public Bitmap? Thumbnail { get; init; }
    public bool HasThumbnail => Thumbnail is not null;

    // The template asks for a newer engine than this launcher belongs to.
    public bool NeedsNewerEngine { get; init; }
    public bool IsAvailable => !NeedsNewerEngine;
}
