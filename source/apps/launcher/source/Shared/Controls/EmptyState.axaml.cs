using Avalonia;
using Avalonia.Controls;
using Avalonia.Markup.Xaml;
using Avalonia.Media;

namespace ECS3DLauncher.Shared.Controls;

public class EmptyState : UserControl
{
    public static readonly StyledProperty<Geometry?> IconDataProperty =
        AvaloniaProperty.Register<EmptyState, Geometry?>(nameof(IconData));

    public static readonly StyledProperty<string?> HeadingProperty =
        AvaloniaProperty.Register<EmptyState, string?>(nameof(Heading));

    public EmptyState() => AvaloniaXamlLoader.Load(this);

    public Geometry? IconData
    {
        get => GetValue(IconDataProperty);
        set => SetValue(IconDataProperty, value);
    }

    public string? Heading
    {
        get => GetValue(HeadingProperty);
        set => SetValue(HeadingProperty, value);
    }
}
