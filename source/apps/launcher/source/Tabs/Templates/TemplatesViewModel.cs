using System.Collections.ObjectModel;
using ECS3DLauncher.Shared.Models;

namespace ECS3DLauncher.Tabs.Templates;

// State for the Templates tab: the starter-template catalog.
public sealed class TemplatesViewModel
{
    public ObservableCollection<ProjectTemplate> Templates { get; } =
    [
        new() { Name = "Empty 3D",         Description = "A blank scene with one camera and a directional light.",     Icon = "IconScene" },
        new() { Name = "Physics Sandbox",  Description = "Ground plane, gravity, and a few rigid bodies ready to drop.", Icon = "IconModel" },
        new() { Name = "First-Person",     Description = "Player controller, collider, and mouse-look script wired up.", Icon = "IconSparkle" },
        new() { Name = "Top-Down",         Description = "Orthographic camera with a movement script and tile floor.",  Icon = "IconGrid" },
        new() { Name = "ECS Starter",      Description = "Component + system scaffolding to build your own gameplay.",   Icon = "IconLayers" },
        new() { Name = "Showcase",         Description = "Lit material demo with the earth model and specular map.",     Icon = "IconModel" },
    ];
}
