using CommunityToolkit.Mvvm.ComponentModel;

namespace ECS3DLauncher.Shell;

// One row in the sidebar's Library list. IsActive drives the accent highlight.
public class NavItemViewModel : ObservableObject
{
    public required string Key { get; init; }
    public required string Label { get; init; }
    public required string Icon { get; init; }
    private string? _count;
    private bool _isActive;

    public string? Count
    {
        get => _count;
        set => SetProperty(ref _count, value);
    }

    public bool IsActive
    {
        get => _isActive;
        set => SetProperty(ref _isActive, value);
    }
}
