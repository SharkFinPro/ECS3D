using System;
using System.Collections.ObjectModel;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ECS3DLauncher.Shared.Models;

namespace ECS3DLauncher.Tabs.Projects;

// State for the Projects tab: the project list and the grid-vs-list toggle.
public partial class ProjectsViewModel : ObservableObject
{
    // The shell owns navigation; the tab only asks it to open Templates.
    private readonly Action _openTemplates;

    public ObservableCollection<Project> Projects { get; }

    // Projects preceded by the "New Project" sentinel, for the item panels.
    public ObservableCollection<Project> ProjectTiles { get; }

    [ObservableProperty]
    private bool _isGridView = true;

    public bool IsListView => !IsGridView;

    public ProjectsViewModel(Action openTemplates)
    {
        _openTemplates = openTemplates;

        Projects =
        [
            new() { Name = "Solar Sandbox",     Path = "~/ecs3d/solar_sandbox",  Edited = "12 minutes ago", Version = "0.0.1", Pinned = true,  Thumbnail = Load("scene_render.png") },
            new() { Name = "Physics Playground", Path = "~/ecs3d/physics_play",  Edited = "3 hours ago",    Version = "0.0.1", Pinned = true,  GlowColor = Color.Parse("#8C1FB8D4"), PlaceholderIcon = "IconScene" },
            new() { Name = "Marble Run",        Path = "~/ecs3d/marble_run",     Edited = "Yesterday",      Version = "0.0.1", GlowColor = Color.Parse("#85A78BFA"), PlaceholderIcon = "IconModel" },
            new() { Name = "Voxel Terrain",     Path = "~/ecs3d/voxel_terrain",  Edited = "2 days ago",     Version = "0.0.1", GlowColor = Color.Parse("#7D5CBF6A"), PlaceholderIcon = "IconModel" },
            new() { Name = "Boids Flock",       Path = "~/ecs3d/boids",          Edited = "Last week",      Version = "0.0.1", GlowColor = Color.Parse("#7DE6B35A"), PlaceholderIcon = "IconScene" },
            new() { Name = "Particle Demo",     Path = "~/ecs3d/particles",      Edited = "Last week",      Version = "0.0.1", GlowColor = Color.Parse("#7AE5565B"), PlaceholderIcon = "IconScene" },
            new() { Name = "Shader Lab",        Path = "~/ecs3d/shader_lab",     Edited = "2 weeks ago",    Version = "0.0.1", GlowColor = Color.Parse("#854D93F5"), PlaceholderIcon = "IconModel" },
        ];

        ProjectTiles = [new() { IsNewCard = true, Name = "", Path = "", Edited = "", Version = "" }];
        foreach (var p in Projects)
            ProjectTiles.Add(p);
    }

    partial void OnIsGridViewChanged(bool value) => OnPropertyChanged(nameof(IsListView));

    [RelayCommand]
    private void SetGrid() => IsGridView = true;

    [RelayCommand]
    private void SetList() => IsGridView = false;

    [RelayCommand]
    private void NewProject() => _openTemplates();

    private static Bitmap? Load(string file)
    {
        try
        {
            var uri = new Uri($"avares://ECS3DLauncher/source/Assets/{file}");
            return new Bitmap(AssetLoader.Open(uri));
        }
        catch
        {
            return null;
        }
    }
}
