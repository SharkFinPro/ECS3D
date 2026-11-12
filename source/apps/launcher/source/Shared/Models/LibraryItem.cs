using System.Collections.Generic;
using Avalonia.Media.Imaging;

namespace ECS3DLauncher.Shared.Models;

public enum LibraryItemKind
{
    Tour,
    Template,
    Sample,
    Notice,
}

// One row of the Library list. Template and Sample carry the card their buttons act on; Notice is the catalog's
// empty explanation, shown where the templates would be.
public sealed class LibraryItem
{
    public required LibraryItemKind Kind { get; init; }
    public required string Name { get; init; }
    public required string Description { get; init; }
    public IReadOnlyList<string> Tags { get; init; } = [];
    public string? PlayersLabel { get; init; }
    public Bitmap? Thumbnail { get; init; }
    public TemplateCard? Template { get; init; }
    public SampleCard? Sample { get; init; }

    public bool HasPlayers => PlayersLabel is not null;
    public bool HasThumbnail => Thumbnail is not null;
    public bool IsNotice => Kind == LibraryItemKind.Notice;
    public bool IsRow => !IsNotice;
    public bool IsTour => Kind == LibraryItemKind.Tour;
    public bool IsTemplate => Kind == LibraryItemKind.Template;
    public bool IsSample => Kind == LibraryItemKind.Sample;
    public bool IsMultiplayerSample => Sample is { IsMultiplayer: true };
    public bool NeedsNewerEngine => Template is { NeedsNewerEngine: true };

    public string KindLabel => Kind switch
    {
        LibraryItemKind.Tour => "Tour",
        LibraryItemKind.Template => "Template",
        LibraryItemKind.Sample => "Sample",
        _ => "",
    };
}
