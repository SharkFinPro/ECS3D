using CommunityToolkit.Mvvm.ComponentModel;

namespace ECS3DLauncher.Shell;

// One row in the sidebar's Library list. IsActive drives the accent highlight.
public partial class NavItemViewModel : ObservableObject
{
    public required string Key { get; init; }
    public required string Label { get; init; }
    public required string Icon { get; init; }
    public string? Count { get; init; }

    [ObservableProperty]
    private bool _isActive;
}
