using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ECS3DLauncher.Tabs.Learn;
using ECS3DLauncher.Tabs.Projects;
using ECS3DLauncher.Tabs.Samples;
using ECS3DLauncher.Tabs.Settings;
using ECS3DLauncher.Tabs.Templates;

namespace ECS3DLauncher.Shell;

// Shell state: window chrome info, sidebar navigation, and which tab is showing.
// Each tab owns its content via its own view model under source/Tabs/*.
public class MainWindowViewModel : ObservableObject
{
    public string Version => "v0.0.1";

    public ObservableCollection<NavItemViewModel> NavItems { get; }

    public ProjectsViewModel Projects { get; }
    public TemplatesViewModel Templates { get; } = new();
    public SamplesViewModel Samples { get; } = new();
    public LearnViewModel Learn { get; } = new();
    public SettingsViewModel Settings { get; } = new();

    // Which library section is showing. CurrentTab is the active tab's view
    // model; the window's ContentControl maps it to a view by type.
    private string _activeNav = "projects";
    private object? _currentTab;

    public string ActiveNav
    {
        get => _activeNav;
        set
        {
            if (!SetProperty(ref _activeNav, value))
            {
                return;
            }

            CurrentTab = value switch
            {
                "templates" => Templates,
                "samples"   => Samples,
                "learn"     => Learn,
                "settings"  => Settings,
                _           => (object)Projects,
            };

            UpdateActiveFlags();
            OnPropertyChanged(nameof(ViewTitle));
            OnPropertyChanged(nameof(ViewSubtitle));
        }
    }

    public object? CurrentTab
    {
        get => _currentTab;
        set => SetProperty(ref _currentTab, value);
    }

    // UI-only; no engine backend yet.
    public IRelayCommand<string?> SelectNavCommand { get; }
    public IRelayCommand NewProjectCommand { get; }

    public MainWindowViewModel()
    {
        SelectNavCommand = new RelayCommand<string?>(key => ActiveNav = key!);
        NewProjectCommand = new RelayCommand(() => ActiveNav = "templates");

        Projects = new ProjectsViewModel(openTemplates: () => ActiveNav = "templates");

        NavItems =
        [
            new() { Key = "projects",  Label = "Projects",  Icon = "IconFolder", Count = Projects.Projects.Count.ToString() },
            new() { Key = "templates", Label = "Templates", Icon = "IconLayers" },
            new() { Key = "samples",   Label = "Samples",   Icon = "IconSparkle" },
            new() { Key = "learn",     Label = "Learn",     Icon = "IconBook" },
        ];

        CurrentTab = Projects;
        UpdateActiveFlags();
    }

    // ----- Derived view state (mirrors the mockup's title map) -----
    public string ViewTitle => ActiveNav switch
    {
        "templates" => "Templates",
        "samples"   => "Samples",
        "learn"     => "Learn",
        "settings"  => "Settings",
        _           => "Recent Projects",
    };

    public string ViewSubtitle => ActiveNav switch
    {
        "templates" => "Start a project from a ready-made scene.",
        "samples"   => "Example projects bundled with ECS3D.",
        "learn"     => "Guides and documentation for the engine.",
        "settings"  => "Editor preferences.",
        _           => "Pick up where you left off, or start something new.",
    };

    private void UpdateActiveFlags()
    {
        foreach (var item in NavItems)
        {
            item.IsActive = item.Key == ActiveNav;
        }
    }
}
