using System;
using System.Collections.ObjectModel;
using System.IO;
using System.Threading.Tasks;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
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
    private readonly SettingsEditor _settings;
    private readonly IProcessRunner _runner;
    private readonly EarlyExitWatcher _watcher;
    private readonly TemplateCatalog _catalog;
    private readonly Action<Action> _post;
    private EngineLocation _engine;
    private EngineActions _actions;
    private LaunchCoordinator _coordinator;

    public string Version { get; }

    public ObservableCollection<NavItemViewModel> NavItems { get; }

    public ProjectsViewModel Projects { get; }
    public TemplatesViewModel Templates { get; } = new();
    public SamplesViewModel Samples { get; }
    public LearnViewModel Learn { get; } = new();
    public SettingsViewModel Settings { get; }

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
                Samples.SearchText = value;
            }
        }
    }

    // The engine-not-found banner: null text means the engine is complete.
    // Re-located when the Settings tab changes the engine folder.
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
    public Func<string, Task<bool>>? OpenLogFile { get; set; }

    public IRelayCommand<string?> SelectNavCommand { get; }
    public IRelayCommand NewProjectCommand { get; }
    public IRelayCommand OpenSettingsCommand { get; }
    public IRelayCommand ExploreCommand { get; }
    public IRelayCommand<Project> OpenProjectCommand { get; }
    public IRelayCommand<SampleCard> PlaySampleCommand { get; }
    public IRelayCommand DismissToastCommand { get; }
    public IRelayCommand OpenLogsCommand { get; }

    public MainWindowViewModel()
        : this(
            NewSettings(),
            new SystemProcessRunner(),
            new EarlyExitWatcher(new UserDataDirectory()),
            new TemplateCatalog(new PhysicalFileSystem(), new FileLauncherLog(new UserDataDirectory())),
            LoadThumbnail,
            AppVersion.Current(),
            action => Dispatcher.UIThread.Post(action))
    {
    }

    public MainWindowViewModel(
        SettingsEditor settings,
        IProcessRunner runner,
        EarlyExitWatcher watcher,
        TemplateCatalog catalog,
        Func<string, Bitmap?> loadThumbnail,
        string version,
        Action<Action> post)
    {
        _post = post;
        _settings = settings;
        _runner = runner;
        _watcher = watcher;
        _catalog = catalog;
        _engine = settings.Engine;
        _actions = new EngineActions(_engine);
        _coordinator = NewCoordinator(_engine);
        Version = VersionLabel.Format(version);

        SelectNavCommand = new RelayCommand<string?>(key => ActiveNav = key!);
        NewProjectCommand = new RelayCommand(() => ActiveNav = "templates");
        OpenSettingsCommand = new RelayCommand(() => ActiveNav = "settings");
        ExploreCommand = new RelayCommand(Explore, () => _actions.Explore.Available);
        OpenProjectCommand = new RelayCommand<Project>(OpenProject, _ => _actions.Open.Available);
        PlaySampleCommand = new RelayCommand<SampleCard>(PlaySample, _ => _actions.Play.Available);
        DismissToastCommand = new RelayCommand(DismissToast);
        OpenLogsCommand = new RelayCommand(() => _ = OpenLogsAsync(), () => HasToastLog);

        Projects = new ProjectsViewModel(
            () => ActiveNav = "templates",
            ExploreCommand,
            OpenProjectCommand,
            _actions.Explore.Tooltip,
            _actions.Open.Tooltip,
            []);
        Samples = new SamplesViewModel(PlaySampleCommand, _actions.Play.Tooltip, loadThumbnail);
        Samples.Reload(_catalog.Load(_engine.Folder));

        NavItems =
        [
            new() { Key = "projects",  Label = "Projects",  Icon = "IconFolder", Count = ProjectCount() },
            new() { Key = "templates", Label = "Templates", Icon = "IconLayers" },
            new() { Key = "samples",   Label = "Samples",   Icon = "IconSparkle", Count = SampleCount() },
            new() { Key = "learn",     Label = "Learn",     Icon = "IconBook" },
        ];
        Projects.Projects.CollectionChanged += (_, _) => NavItems[0].Count = ProjectCount();

        Projects.IsGridView = settings.Current.ProjectsView != LauncherSettings.ListView;
        Projects.ViewChosen = grid => ApplyUpdate(_settings.SetProjectsView(grid));
        Settings = new SettingsViewModel(settings, ApplyUpdate, message => ApplyNotice(EarlyExitNotice.Failure(message)));

        CurrentTab = Projects;
        UpdateActiveFlags();
    }

    private static SettingsEditor NewSettings()
    {
        var fileSystem = new PhysicalFileSystem();
        var userData = new UserDataDirectory();
        var store = new LauncherSettingsStore(fileSystem, new FileLauncherLog(userData), userData);
        return new SettingsEditor(store, new EngineLocator(fileSystem));
    }

    private static Bitmap? LoadThumbnail(string path)
    {
        try
        {
            return new Bitmap(path);
        }
        catch (Exception exception) when (
            exception is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException
                or InvalidOperationException)
        {
            return null;
        }
    }

    private LaunchCoordinator NewCoordinator(EngineLocation engine) =>
        new(new EngineLauncher(_runner, engine), _watcher, ShowNotice);

    // A failed save shows the toast; a moved engine folder replaces the located engine and what depends on it.
    private void ApplyUpdate(SettingsUpdate update)
    {
        if (update.SaveFailed)
        {
            ApplyNotice(EarlyExitNotice.Failure($"Could not save the launcher settings to {_settings.FilePath}"));
        }

        if (update.Engine is { } engine)
        {
            ApplyEngine(engine);
        }
    }

    private void ApplyEngine(EngineLocation engine)
    {
        _engine = engine;
        _actions = new EngineActions(engine);
        _coordinator = NewCoordinator(engine);
        Projects.UpdateTooltips(_actions.Explore.Tooltip, _actions.Open.Tooltip);
        ExploreCommand.NotifyCanExecuteChanged();
        OpenProjectCommand.NotifyCanExecuteChanged();
        Samples.UpdatePlay(_actions.Play.Tooltip);
        Samples.Reload(_catalog.Load(engine.Folder));
        NavItems[2].Count = SampleCount();
        PlaySampleCommand.NotifyCanExecuteChanged();
        OnPropertyChanged(nameof(EngineBannerText));
        OnPropertyChanged(nameof(HasEngineBanner));
    }

    // ----- Derived view state: the header table, per tab -----
    public string ViewTitle => Header.Title;
    public string ViewSubtitle => Header.Subtitle;
    public string SearchPlaceholder => Header.SearchPlaceholder;
    public bool ShowSearch => Header.ShowSearch;
    public bool ShowViewToggle => Header.ShowViewToggle;

    private TabHeader Header => LauncherTabs.Header(LauncherTabs.Parse(ActiveNav));

    private string ProjectCount() => Projects.Projects.Count.ToString();

    private string SampleCount() => Samples.Samples.Count.ToString();

    // The task is dropped on purpose: failures and early exits arrive through ShowNotice.
    private void Explore() => _ = _coordinator.ExploreEngineAsync(_settings.Current.ToLaunchOptions());

    private void OpenProject(Project? project)
    {
        if (project is { IsRealCard: true } && _actions.Open.Available)
        {
            _ = _coordinator.OpenProjectAsync(project.Path, _settings.Current.ToLaunchOptions());
        }
    }

    private void PlaySample(SampleCard? card)
    {
        if (card is not null && _actions.Play.Available)
        {
            _ = _coordinator.PlayProjectAsync(card.FolderPath);
        }
    }

    // Notices come from background continuations, so they are handed to the UI thread explicitly.
    private void ShowNotice(LaunchNotice notice) => _post(() => ApplyNotice(notice));

    private void ApplyNotice(LaunchNotice notice)
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

    private async Task OpenLogsAsync()
    {
        var path = ToastLogPath;
        if (path is null)
        {
            return;
        }

        var opened = false;
        try
        {
            opened = OpenLogFile is not null && await OpenLogFile(path);
        }
        catch (Exception exception) when (exception is not OperationCanceledException)
        {
            opened = false;
        }

        if (!opened)
        {
            ApplyNotice(EarlyExitNotice.Failure($"Could not open the log file {path}"));
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
