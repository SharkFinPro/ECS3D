using Avalonia.Media;
using CommunityToolkit.Mvvm.Input;
using ECS3DLauncher.Shell;
using ECS3DLauncher.Shared.Models;
using ECS3DLauncher.Tabs.Projects;
using ECS3DLauncher.Tabs.Templates;

namespace ECS3DLauncher.Shared;

// Design-time view models. Referenced via d:DataContext so the Rider/Avalonia
// previewer renders real content when a view is opened on its own, with no
// runtime DataContext. Stripped from the app at runtime - design-only.
public static class DesignData
{
    public static MainWindowViewModel MainWindow { get; } = new();

    public static ProjectsViewModel Projects { get; } = new(
        () => { },
        new RelayCommand(() => { }),
        new RelayCommand<Project>(_ => { }),
        null,
        null,
        [
            new()
            {
                Name = "Physics Playground", Path = "C:/Projects/physics_play", Edited = "3 hours ago",
                Pinned = true, GlowColor = Color.Parse("#8C1FB8D4"), PlaceholderIcon = "IconScene",
            },
            new()
            {
                Name = "Marble Run", Path = "C:/Projects/marble_run", Edited = "Yesterday",
                GlowColor = Color.Parse("#85A78BFA"), PlaceholderIcon = "IconModel",
            },
        ]);

    public static TemplatesViewModel Templates { get; } = new();
}
