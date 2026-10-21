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

    // The shell owns navigation; the tab only asks it to open Templates.
    private readonly Action _openTemplates;
    private string _searchText = "";
    private bool _isGridView = true;

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

    public string? ExploreTooltip { get; }

    public IRelayCommand SetGridCommand { get; }
    public IRelayCommand SetListCommand { get; }
    public IRelayCommand NewProjectCommand { get; }
    public IRelayCommand ExploreCommand { get; }
    public IRelayCommand<Project> OpenProjectCommand { get; }

    public ProjectsViewModel(
        Action openTemplates,
        IRelayCommand exploreCommand,
        IRelayCommand<Project> openProjectCommand,
        string? exploreTooltip,
        IEnumerable<Project> projects)
    {
        _openTemplates = openTemplates;
        ExploreCommand = exploreCommand;
        OpenProjectCommand = openProjectCommand;
        ExploreTooltip = exploreTooltip;
        SetGridCommand = new RelayCommand(() => IsGridView = true);
        SetListCommand = new RelayCommand(() => IsGridView = false);
        NewProjectCommand = new RelayCommand(() => _openTemplates());

        Projects = new ObservableCollection<Project>(projects);
        Projects.CollectionChanged += (_, _) =>
        {
            RebuildTiles();
            OnPropertyChanged(nameof(IsEmpty));
            OnPropertyChanged(nameof(ShowGrid));
            OnPropertyChanged(nameof(ShowList));
        };
        RebuildTiles();
    }

    private void RebuildTiles()
    {
        ProjectTiles.Clear();
        ProjectTiles.Add(NewTile);
        foreach (var project in Projects.Where(item => SearchMatch.Matches(_searchText, item.Name, item.Path)))
        {
            ProjectTiles.Add(project);
        }
    }
}
