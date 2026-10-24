using System.Collections.Generic;
using Avalonia.Media.Imaging;

namespace ECS3DLauncher.Shared.Models;

// A sample project card: the manifest fields the Samples tab shows, plus the shipped folder Play opens.
public sealed class SampleCard
{
    public required string Id { get; init; }
    public required string Name { get; init; }
    public required string Description { get; init; }
    public required IReadOnlyList<string> Tags { get; init; }
    public required string FolderPath { get; init; }

    // "1-4 players" for a multiplayer sample; null for a single-player one.
    public string? PlayersLabel { get; init; }
    public bool HasPlayers => PlayersLabel is not null;

    // True when the manifest allows more than one player, which is when the card offers Host.
    public bool IsMultiplayer { get; init; }

    public Bitmap? Thumbnail { get; init; }
    public bool HasThumbnail => Thumbnail is not null;
}
