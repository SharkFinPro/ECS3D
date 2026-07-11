using ECS3DLauncher.Shell;
using ECS3DLauncher.Tabs.Projects;
using ECS3DLauncher.Tabs.Templates;

namespace ECS3DLauncher.Shared;

// Design-time view models. Referenced via d:DataContext so the Rider/Avalonia
// previewer renders real content when a view is opened on its own, with no
// runtime DataContext. Stripped from the app at runtime - design-only.
public static class DesignData
{
    public static MainWindowViewModel MainWindow { get; } = new();
    public static ProjectsViewModel Projects { get; } = new(openTemplates: () => { });
    public static TemplatesViewModel Templates { get; } = new();
}
