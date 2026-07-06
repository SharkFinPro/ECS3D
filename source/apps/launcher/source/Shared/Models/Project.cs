using Avalonia;
using Avalonia.Media;
using Avalonia.Media.Imaging;

namespace ECS3DLauncher.Shared.Models;

// A project tile in the launcher. Either shows a real thumbnail image, or a
// generated placeholder (a colored glow + a faint glyph) like the mockup.
public sealed class Project
{
    public required string Name { get; init; }
    public required string Path { get; init; }
    public required string Edited { get; init; }
    public required string Version { get; init; }
    public bool Pinned { get; init; }

    // The leading "New Project" tile is modeled as a sentinel so it flows inline
    // with the real cards in the same items panel.
    public bool IsNewCard { get; init; }
    public bool IsRealCard => !IsNewCard;

    public Bitmap? Thumbnail { get; init; }
    public bool HasThumbnail => Thumbnail is not null;

    // Placeholder styling (used when Thumbnail is null).
    public Color GlowColor { get; init; }
    public string PlaceholderIcon { get; init; } = "IconScene";

    // The radial glow is built here rather than in XAML: bindings inside a Brush
    // (its GradientStops) don't inherit the item's DataContext, so a bound
    // gradient stop silently stays empty. Constructing the brush in code sidesteps
    // that and lets the view bind Border.Background directly.
    public IBrush? GlowBrush => GlowColor.A == 0
        ? null
        : new RadialGradientBrush
        {
            Center = new RelativePoint(0.3, 0.22, RelativeUnit.Relative),
            GradientOrigin = new RelativePoint(0.3, 0.22, RelativeUnit.Relative),
            RadiusX = new RelativeScalar(0.85, RelativeUnit.Relative),
            RadiusY = new RelativeScalar(0.85, RelativeUnit.Relative),
            GradientStops =
            {
                new GradientStop(GlowColor, 0),
                new GradientStop(Colors.Transparent, 0.75),
            },
        };
}

// A starter template card.
public sealed class ProjectTemplate
{
    public required string Name { get; init; }
    public required string Description { get; init; }
    public required string Icon { get; init; }
}
