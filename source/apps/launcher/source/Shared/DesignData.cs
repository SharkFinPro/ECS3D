using Avalonia.Media;
using CommunityToolkit.Mvvm.Input;
using ECS3DLauncher.Services;
using ECS3DLauncher.Shell;
using ECS3DLauncher.Shared.Models;
using ECS3DLauncher.Tabs.Learn;
using ECS3DLauncher.Tabs.Projects;
using ECS3DLauncher.Tabs.Samples;
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

    public static TemplatesViewModel Templates { get; } = NewTemplates();

    private static TemplatesViewModel NewTemplates()
    {
        var model = new TemplatesViewModel("0.1.0", _ => null);
        var empty = new TemplateManifest(
            "empty_3d", "Empty 3D", "A blank scene with one camera and a directional light.",
            "template", ["starter"], "thumbnail.png", "Main", "0.1.0");
        var arena = new TemplateManifest(
            "arena", "Arena", "A small multiplayer arena with spawn points.",
            "template", ["multiplayer"], "thumbnail.png", "Main", "0.1.0")
        {
            Players = new TemplatePlayers(1, 4),
        };
        var future = new TemplateManifest(
            "future", "Future Template", "Needs an engine that has not shipped yet.",
            "template", ["preview"], "thumbnail.png", "Main", "9.0.0");
        model.Reload(new TemplateCatalogContents(
            "C:/ECS3D/templates", true,
            [
                new TemplateEntry(empty, "C:/ECS3D/templates/empty_3d", null),
                new TemplateEntry(arena, "C:/ECS3D/templates/arena", null),
                new TemplateEntry(future, "C:/ECS3D/templates/future", null),
            ],
            []));
        return model;
    }

    public static SamplesViewModel Samples { get; } = NewSamples();

    private static SamplesViewModel NewSamples()
    {
        var model = new SamplesViewModel(
            new RelayCommand<SampleCard>(_ => { }), null, _ => null, new RelayCommand<SampleCard>(_ => { }), null);
        var manifest = new TemplateManifest(
            "coin_rush", "Coin Rush", "Collect every coin before the timer runs out, alone or with friends.",
            "sample", ["multiplayer", "scripting"], "thumbnail.png", "Main", "0.1.0")
        {
            Players = new TemplatePlayers(1, 4),
        };
        model.Reload(new TemplateCatalogContents(
            "C:/ECS3D/templates", true, [], [new TemplateEntry(manifest, "C:/ECS3D/templates/coin_rush", null)]));
        return model;
    }

    public static LearnViewModel Learn { get; } =
        new(_ => { }, () => { }, new RelayCommand(() => { }), null);
}
