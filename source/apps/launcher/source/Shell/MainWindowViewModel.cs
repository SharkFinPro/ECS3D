using System;
using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ECS3DLauncher.Services;
using ECS3DLauncher.Shared.Models;
using ECS3DLauncher.Tabs.Learn;
using ECS3DLauncher.Tabs.Projects;
using ECS3DLauncher.Tabs.Samples;
using ECS3DLauncher.Tabs.Settings;
using ECS3DLauncher.Tabs.Templates;

namespace ECS3DLauncher.Shell;

// Shell state: window chrome info, sidebar navigation, the per-tab header, the engine banner and the toast.
// Each tab owns its content via its own view model under source/Tabs/*.
public class MainWindowViewModel : ObservableObject
{
    // Launch options come from the Settings tab once it exists.
    private static readonly LaunchOptions DefaultLaunchOptions = new(false, false);

    private readonly EngineLocation _engine;
    private readonly EngineActions _actions;
    private readonly LaunchCoordinator _coordinator;

    public string Version { get; }

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
    private string _searchText = "";
    private string? _toastMessage;
    private string? _toastLogPath;

    public string ActiveNav
    {
        get => _activeNav;
        set
        {
            if (!SetProperty(ref _activeNav, value))
            {
                return;
            }

            CurrentTab = LauncherTabs.Parse(value) switch
            {
                LauncherTab.Templates => Templates,
                LauncherTab.Samples => Samples,
                LauncherTab.Learn => Learn,
                LauncherTab.Settings => Settings,
                _ => (object)Projects,
            };

            SearchText = "";
            UpdateActiveFlags();
            OnPropertyChanged(nameof(ViewTitle));
            OnPropertyChanged(nameof(ViewSubtitle));
            OnPropertyChanged(nameof(SearchPlaceholder));
            OnPropertyChanged(nameof(ShowSearch));
            OnPropertyChanged(nameof(ShowViewToggle));
        }
    }

    public object? CurrentTab
    {
        get => _currentTab;
        set => SetProperty(ref _currentTab, value);
    }

    public string SearchText
    {
        get => _searchText;
        set
        {
            if (SetProperty(ref _searchText, value))
            {
                Projects.SearchText = value;
            }
        }
    }

    // The engine-not-found banner: null text means the engine is complete.
    public string? EngineBannerText => _engine.Describe();
    public bool HasEngineBanner => EngineBannerText is not null;

    public string? ToastMessage
    {
        get => _toastMessage;
        private set
        {
            if (SetProperty(ref _toastMessage, value))
            {
                OnPropertyChanged(nameof(HasToast));
            }
        }
    }

    public string? ToastLogPath
    {
        get => _toastLogPath;
        private set
        {
            if (SetProperty(ref _toastLogPath, value))
            {
                OnPropertyChanged(nameof(HasToastLog));
            }
        }
    }

    public bool HasToast => ToastMessage is not null;
    public bool HasToastLog => ToastLogPath is not null;

    // Set by the window, which owns the OS "open with the default app" call.
    public Action<string>? OpenLogFile { get; set; }

    public IRelayCommand<string?> SelectNavCommand { get; }
    public IRelayCommand NewProjectCommand { get; }
    public IRelayCommand OpenSettingsCommand { get; }
    public IRelayCommand ExploreCommand { get; }
    public IRelayCommand<Project> OpenProjectCommand { get; }
    public IRelayCommand DismissToastCommand { get; }
    public IRelayCommand OpenLogsCommand { get; }

    public MainWindowViewModel()
        : this(
            new EngineLocator(new PhysicalFileSystem()).Locate(null),
            new SystemProcessRunner(),
            new EarlyExitWatcher(new UserDataDirectory()),
            AppVersion.Current())
    {
    }

    public MainWindowViewModel(
        EngineLocation engine,
        IProcessRunner runner,
        EarlyExitWatcher watcher,
        string version)
    {
        _engine = engine;
        _actions = new EngineActions(engine);
        _coordinator = new LaunchCoordinator(new EngineLauncher(runner, engine), watcher, ShowNotice);
        Version = VersionLabel.Format(version);

        SelectNavCommand = new RelayCommand<string?>(key => ActiveNav = key!);
        NewProjectCommand = new RelayCommand(() => ActiveNav = "templates");
        OpenSettingsCommand = new RelayCommand(() => ActiveNav = "settings");
        ExploreCommand = new RelayCommand(Explore, () => _actions.Explore.Available);
        OpenProjectCommand = new RelayCommand<Project>(OpenProject, _ => _actions.Open.Available);
        DismissToastCommand = new RelayCommand(DismissToast);
        OpenLogsCommand = new RelayCommand(OpenLogs, () => HasToastLog);

        Projects = new ProjectsViewModel(
            () => ActiveNav = "templates",
            ExploreCommand,
            OpenProjectCommand,
            _actions.Explore.Tooltip,
            []);

        NavItems =
        [
            new() { Key = "projects",  Label = "Projects",  Icon = "IconFolder", Count = ProjectCount() },
            new() { Key = "templates", Label = "Templates", Icon = "IconLayers" },
            new() { Key = "samples",   Label = "Samples",   Icon = "IconSparkle" },
            new() { Key = "learn",     Label = "Learn",     Icon = "IconBook" },
        ];
        Projects.Projects.CollectionChanged += (_, _) => NavItems[0].Count = ProjectCount();

        CurrentTab = Projects;
        UpdateActiveFlags();
    }

    // ----- Derived view state: the header table, per tab -----
    public string ViewTitle => Header.Title;
    public string ViewSubtitle => Header.Subtitle;
    public string SearchPlaceholder => Header.SearchPlaceholder;
    public bool ShowSearch => Header.ShowSearch;
    public bool ShowViewToggle => Header.ShowViewToggle;

    private TabHeader Header => LauncherTabs.Header(LauncherTabs.Parse(ActiveNav));

    private string ProjectCount() => Projects.Projects.Count.ToString();

    // The task is dropped on purpose: failures and early exits arrive through ShowNotice.
    private void Explore() => _ = _coordinator.ExploreEngineAsync(DefaultLaunchOptions);

    private void OpenProject(Project? project)
    {
        if (project is not null)
        {
            _ = _coordinator.OpenProjectAsync(project.Path, DefaultLaunchOptions);
        }
    }

    private void ShowNotice(LaunchNotice notice)
    {
        ToastLogPath = notice.LogPath;
        ToastMessage = notice.Message;
        OpenLogsCommand.NotifyCanExecuteChanged();
    }

    private void DismissToast()
    {
        ToastMessage = null;
        ToastLogPath = null;
        OpenLogsCommand.NotifyCanExecuteChanged();
    }

    private void OpenLogs()
    {
        if (ToastLogPath is not null)
        {
            OpenLogFile?.Invoke(ToastLogPath);
        }
    }

    private void UpdateActiveFlags()
    {
        foreach (var item in NavItems)
        {
            item.IsActive = item.Key == ActiveNav;
        }
    }
}
