using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ECS3DLauncher.Services;
using ECS3DLauncher.Shared.Models;

namespace ECS3DLauncher.Tabs.Projects;

// State for the Projects tab: the project list and the grid-vs-list toggle.
public class ProjectsViewModel : ObservableObject
{
    private static readonly Project NewTile = new() { IsNewCard = true, Name = "", Path = "", Edited = "" };

    // The shell owns the New Project dialog; the tab only asks it to open.
    private readonly Action _openNewProject;
    private string _searchText = "";
    private bool _isGridView = true;
    private string? _exploreTooltip;
    private string? _openTooltip;
    private string? _playTooltip;
    private string? _hostTooltip;
    private bool _replacing;

    public ObservableCollection<Project> Projects { get; }

    // The "New Project" sentinel followed by the projects matching the search, for the item panels.
    public ObservableCollection<Project> ProjectTiles { get; } = [];

    public bool IsGridView
    {
        get => _isGridView;
        set
        {
            if (SetProperty(ref _isGridView, value))
            {
                ViewChosen?.Invoke(value);
                OnPropertyChanged(nameof(IsListView));
                OnPropertyChanged(nameof(ShowGrid));
                OnPropertyChanged(nameof(ShowList));
            }
        }
    }

    public bool IsListView => !IsGridView;

    public bool IsEmpty => Projects.Count == 0;

    public bool ShowGrid => !IsEmpty && IsGridView;

    public bool ShowList => !IsEmpty && IsListView;

    // Projects are stored but the search hides every one of them; only the New Project tile is left.
    public bool ShowNoMatches => !IsEmpty && ProjectTiles.Count == 1;

    public string SearchText
    {
        get => _searchText;
        set
        {
            if (SetProperty(ref _searchText, value))
            {
                RebuildTiles();
            }
        }
    }

    // Called with true for the grid when the choice changes; the shell saves it.
    public Action<bool>? ViewChosen { get; set; }

    public string? ExploreTooltip
    {
        get => _exploreTooltip;
        private set => SetProperty(ref _exploreTooltip, value);
    }

    public string? OpenTooltip
    {
        get => _openTooltip;
        private set => SetProperty(ref _openTooltip, value);
    }

    public string? PlayTooltip
    {
        get => _playTooltip;
        private set => SetProperty(ref _playTooltip, value);
    }

    public string? HostTooltip
    {
        get => _hostTooltip;
        private set => SetProperty(ref _hostTooltip, value);
    }

    // The card menu and first-run commands are set by the shell, which owns the store and the engine.
    public IRelayCommand<Project> PlayProjectCommand { get; set; } = new RelayCommand<Project>(_ => { });
    public IRelayCommand<Project> HostProjectCommand { get; set; } = new RelayCommand<Project>(_ => { });
    public IRelayCommand<Project> ShowInFolderCommand { get; set; } = new RelayCommand<Project>(_ => { });
    public IRelayCommand<Project> TogglePinCommand { get; set; } = new RelayCommand<Project>(_ => { });
    public IRelayCommand<Project> LocateProjectCommand { get; set; } = new RelayCommand<Project>(_ => { });
    public IRelayCommand<Project> RemoveProjectCommand { get; set; } = new RelayCommand<Project>(_ => { });
    public IRelayCommand BrowseSamplesCommand { get; set; } = new RelayCommand(() => { });

    public IRelayCommand SetGridCommand { get; }
    public IRelayCommand SetListCommand { get; }
    public IRelayCommand NewProjectCommand { get; }
    public IRelayCommand ExploreCommand { get; }
    public IRelayCommand<Project> OpenProjectCommand { get; }

    public ProjectsViewModel(
        Action openNewProject,
        IRelayCommand exploreCommand,
        IRelayCommand<Project> openProjectCommand,
        string? exploreTooltip,
        string? openTooltip,
        IEnumerable<Project> projects)
    {
        _openNewProject = openNewProject;
        ExploreCommand = exploreCommand;
        OpenProjectCommand = openProjectCommand;
        ExploreTooltip = exploreTooltip;
        OpenTooltip = openTooltip;
        SetGridCommand = new RelayCommand(() => IsGridView = true);
        SetListCommand = new RelayCommand(() => IsGridView = false);
        NewProjectCommand = new RelayCommand(() => _openNewProject());

        Projects = new ObservableCollection<Project>(projects);
        Projects.CollectionChanged += (_, _) =>
        {
            if (_replacing)
            {
                return;
            }

            RebuildTiles();
            OnPropertyChanged(nameof(IsEmpty));
            OnPropertyChanged(nameof(ShowGrid));
            OnPropertyChanged(nameof(ShowList));
        };
        RebuildTiles();
    }

    public void UpdateTooltips(string? exploreTooltip, string? openTooltip, string? playTooltip)
    {
        ExploreTooltip = exploreTooltip;
        OpenTooltip = openTooltip;
        PlayTooltip = playTooltip;
    }

    public void UpdateHostTooltip(string? hostTooltip) => HostTooltip = hostTooltip;

    // Replaces the whole list and rebuilds the tiles once, keeping the current search.
    public void SetProjects(IReadOnlyList<Project> projects)
    {
        _replacing = true;
        try
        {
            Projects.Clear();
            foreach (var project in projects)
            {
                Projects.Add(project);
            }
        }
        finally
        {
            _replacing = false;
        }

        RebuildTiles();
        OnPropertyChanged(nameof(IsEmpty));
        OnPropertyChanged(nameof(ShowGrid));
        OnPropertyChanged(nameof(ShowList));
    }

    private void RebuildTiles()
    {
        ProjectTiles.Clear();
        ProjectTiles.Add(NewTile);
        foreach (var project in Projects.Where(item => RecentProjectsView.Filter([ToEntry(item)], _searchText).Count > 0))
        {
            ProjectTiles.Add(project);
        }

        OnPropertyChanged(nameof(ShowNoMatches));
    }

    private static RecentProject ToEntry(Project project) =>
        new(project.Path, project.Name, "", project.Pinned, null);
}
