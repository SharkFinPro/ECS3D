using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Media;
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
    private readonly RecentProjectsStore _recents;
    private readonly Func<DateTimeOffset> _now;
    private readonly Func<string, Bitmap?> _loadThumbnail;
    private readonly IFileSystem _fileSystem;
    private readonly Action<Action> _runInBackground;
    private readonly ProjectCreator _creator;
    private IReadOnlyList<TemplateEntry> _templates = [];
    private IReadOnlyList<TemplateEntry> _samples = [];
    private string _templatesFolder = "";
    private Dictionary<string, bool> _missing = [];
    private readonly RunningEditors _editors;
    private List<(RecentProject Entry, string Edited, bool Missing, bool InEditor)> _shown = [];
    private EngineLocation _engine;
    private EngineActions _actions;
    private LaunchCoordinator _coordinator;

    // One instance, so the log file is truncated once per launch rather than by each user of it.
    private static readonly FileLauncherLog SharedLog = new(new UserDataDirectory());

    private const string DefaultTemplateId = "empty-3d";

    private NewProjectViewModel? _newProject;

    public ObservableCollection<NavItemViewModel> NavItems { get; }

    public ProjectsViewModel Projects { get; }
    public TemplatesViewModel Templates { get; }
    public SamplesViewModel Samples { get; }
    public LearnViewModel Learn { get; }
    public SettingsViewModel Settings { get; }
    public HostingViewModel Hosting { get; }

    // Which library section is showing. CurrentTab is the active tab's view
    // model; the window's ContentControl maps it to a view by type.
    private string _activeNav = "projects";
    private object? _currentTab;
    private string _searchText = "";
    private string? _toastMessage;
    private string? _toastLogPath;
    private ConfirmPromptViewModel? _prompt;
    private ConfirmPromptViewModel? _quitPrompt;
    private bool _quitApproved;
    private JoinDialogViewModel? _joinDialog;

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
            OnPropertyChanged(nameof(ShowOpenExisting));
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
                Templates.SearchText = value;
                Samples.SearchText = value;
                Learn.SearchText = value;
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

    // A modal question over the whole window; null when none is asked.
    public ConfirmPromptViewModel? Prompt
    {
        get => _prompt;
        private set
        {
            if (SetProperty(ref _prompt, value))
            {
                OnPropertyChanged(nameof(HasPrompt));
                OnPropertyChanged(nameof(IsBackgroundEnabled));
            }
        }
    }

    public bool HasPrompt => Prompt is not null;

    // The window behind a modal overlay takes no input.
    public bool IsBackgroundEnabled => !HasPrompt && JoinDialog is null && !Hosting.IsPanelOpen;
    // The open Join a game dialog, or null.
    public JoinDialogViewModel? JoinDialog
    {
        get => _joinDialog;
        private set
        {
            if (SetProperty(ref _joinDialog, value))
            {
                OnPropertyChanged(nameof(IsBackgroundEnabled));
            }
        }
    }

    // Names the missing client while Join a game is disabled.
    public string? JoinTooltip => _actions.Play.Tooltip;

    public bool HasToast => ToastMessage is not null;
    public bool HasToastLog => ToastLogPath is not null;

    // Set by the window, which owns the OS "open with the default app" call.
    public Func<string, Task<bool>>? OpenLogFile { get; set; }

    // Set by the window, which owns showing a folder in the file manager.
    public Func<string, Task<bool>>? ShowFolder { get; set; }

    // Set by the window, which owns the OS "open in the default browser" call.
    public Func<string, Task<bool>>? OpenUrl { get; set; }

    // Set by the window, which owns the pickers; each returns the chosen path, null when cancelled and an empty
    // string when the item has no local path.
    public Func<Task<string?>>? PickProjectFolder { get; set; }

    public Func<Task<string?>>? PickProjectFile { get; set; }

    public Func<Task<string?>>? PickProjectsFolder { get; set; }

    // The New Project dialog while it is open; null hides it.
    public NewProjectViewModel? NewProject
    {
        get => _newProject;
        private set
        {
            if (SetProperty(ref _newProject, value))
            {
                OnPropertyChanged(nameof(IsInteractive));
            }
        }
    }

    // False while the New Project dialog is open: the window disables everything behind it, and the commands
    // below ignore calls that still reach them, such as a key press.
    public bool IsInteractive => NewProject is null;

    public IRelayCommand<string?> SelectNavCommand { get; }
    public IRelayCommand NewProjectCommand { get; }
    public IRelayCommand OpenSettingsCommand { get; }
    public IRelayCommand ExploreCommand { get; }
    public IRelayCommand<TemplateCard> OpenTemplateCommand { get; }
    public IRelayCommand<Project> OpenProjectCommand { get; }
    public IRelayCommand<Project> PlayProjectCommand { get; }
    // Set by the window, which owns closing and minimizing itself.
    public Action? CloseWindow { get; set; }

    public Action? MinimizeWindow { get; set; }

    // How long the New Project dialog waits after an edit before it looks at the disk.
    public Func<TimeSpan, Task> NewProjectCheckDelay { get; set; } = span => Task.Delay(span);

    public IRelayCommand CloseOverlayCommand { get; }
    public IRelayCommand<Project> HostProjectCommand { get; }
    public IRelayCommand<Project> ShowInFolderCommand { get; }
    public IRelayCommand<Project> TogglePinCommand { get; }
    public IRelayCommand<Project> RemoveProjectCommand { get; }
    public IRelayCommand<Project> LocateProjectCommand { get; }
    public IRelayCommand OpenExistingCommand { get; }
    public IRelayCommand<SampleCard> PlaySampleCommand { get; }
    public IRelayCommand<SampleCard> HostSampleCommand { get; }
    public IRelayCommand<SampleCard> OpenSampleCopyCommand { get; }
    public IRelayCommand OpenJoinCommand { get; }
    public IRelayCommand CloseJoinCommand { get; }
    public IRelayCommand DismissToastCommand { get; }
    public IRelayCommand OpenLogsCommand { get; }

    public MainWindowViewModel()
        : this(
            NewSettings(),
            NewRecents(),
            new SystemProcessRunner(),
            new EarlyExitWatcher(new UserDataDirectory()),
            new TemplateCatalog(new PhysicalFileSystem(), SharedLog),
            LoadThumbnail,
            AppVersion.Current(),
            action => Dispatcher.UIThread.Post(action),
            TimeProvider.System.GetUtcNow)
    {
    }

    public MainWindowViewModel(
        SettingsEditor settings,
        RecentProjectsStore recents,
        IProcessRunner runner,
        EarlyExitWatcher watcher,
        TemplateCatalog catalog,
        Func<string, Bitmap?> loadThumbnail,
        string version,
        Action<Action> post,
        Func<DateTimeOffset> now)
        : this(
            settings,
            recents,
            runner,
            watcher,
            catalog,
            loadThumbnail,
            version,
            post,
            now,
            new PhysicalFileSystem(),
            work => _ = Task.Run(work))
    {
    }

    public MainWindowViewModel(
        SettingsEditor settings,
        RecentProjectsStore recents,
        IProcessRunner runner,
        EarlyExitWatcher watcher,
        TemplateCatalog catalog,
        Func<string, Bitmap?> loadThumbnail,
        string version,
        Action<Action> post,
        Func<DateTimeOffset> now,
        IFileSystem fileSystem,
        Action<Action> runInBackground)
        : this(
            settings,
            recents,
            runner,
            watcher,
            catalog,
            loadThumbnail,
            version,
            post,
            now,
            fileSystem,
            runInBackground,
            new ProjectCreator(fileSystem, SharedLog))
    {
    }

    // runInBackground runs the disk checks and the project copy off the UI thread; a dead network share can block
    // them for seconds.
    public MainWindowViewModel(
        SettingsEditor settings,
        RecentProjectsStore recents,
        IProcessRunner runner,
        EarlyExitWatcher watcher,
        TemplateCatalog catalog,
        Func<string, Bitmap?> loadThumbnail,
        string version,
        Action<Action> post,
        Func<DateTimeOffset> now,
        IFileSystem fileSystem,
        Action<Action> runInBackground,
        ProjectCreator creator)
        : this(
            settings,
            recents,
            runner,
            watcher,
            catalog,
            loadThumbnail,
            version,
            post,
            now,
            fileSystem,
            runInBackground,
            creator,
            HostingServices.System)
    {
    }

    public MainWindowViewModel(
        SettingsEditor settings,
        RecentProjectsStore recents,
        IProcessRunner runner,
        EarlyExitWatcher watcher,
        TemplateCatalog catalog,
        Func<string, Bitmap?> loadThumbnail,
        string version,
        Action<Action> post,
        Func<DateTimeOffset> now,
        IFileSystem fileSystem,
        Action<Action> runInBackground,
        HostingServices hosting)
        : this(
            settings,
            recents,
            runner,
            watcher,
            catalog,
            loadThumbnail,
            version,
            post,
            now,
            fileSystem,
            runInBackground,
            new ProjectCreator(fileSystem, SharedLog),
            hosting)
    {
    }

    public MainWindowViewModel(
        SettingsEditor settings,
        RecentProjectsStore recents,
        IProcessRunner runner,
        EarlyExitWatcher watcher,
        TemplateCatalog catalog,
        Func<string, Bitmap?> loadThumbnail,
        string version,
        Action<Action> post,
        Func<DateTimeOffset> now,
        IFileSystem fileSystem,
        Action<Action> runInBackground,
        ProjectCreator creator,
        HostingServices hosting)
    {
        _creator = creator;
        _post = post;
        _fileSystem = fileSystem;
        _runInBackground = runInBackground;
        _editors = new RunningEditors(recents.ProjectKey, () => _post(ShowStored));
        _recents = recents;
        _now = now;
        _loadThumbnail = loadThumbnail;
        _settings = settings;
        _runner = runner;
        _watcher = watcher;
        _catalog = catalog;
        _engine = settings.Engine;
        _actions = new EngineActions(_engine);
        _coordinator = NewCoordinator(_engine);

        SelectNavCommand = new RelayCommand<string?>(key =>
        {
            if (IsInteractive)
            {
                ActiveNav = key!;
            }
        });
        NewProjectCommand = new RelayCommand(OpenDefaultNewProject);
        OpenTemplateCommand = new RelayCommand<TemplateCard>(OpenTemplate, card => card is { IsAvailable: true });
        OpenSettingsCommand = new RelayCommand(() => ActiveNav = "settings");
        ExploreCommand = new RelayCommand(Explore, () => _actions.Explore.Available);
        OpenProjectCommand = new RelayCommand<Project>(OpenProject, project => _actions.Open.Available && IsPresent(project));
        PlayProjectCommand = new RelayCommand<Project>(PlayProject, project => _actions.Play.Available && IsPresent(project));
        CloseOverlayCommand = new RelayCommand(CloseOverlay);
        HostProjectCommand = new RelayCommand<Project>(HostProject, project => _actions.Host.Available && IsPresent(project));
        ShowInFolderCommand = new RelayCommand<Project>(project => _ = ShowInFolderAsync(project), IsPresent);
        TogglePinCommand = new RelayCommand<Project>(TogglePin);
        RemoveProjectCommand = new RelayCommand<Project>(RemoveProject);
        LocateProjectCommand = new RelayCommand<Project>(project => _ = LocateAsync(project));
        OpenExistingCommand = new RelayCommand(() => _ = OpenExistingAsync());
        PlaySampleCommand = new RelayCommand<SampleCard>(PlaySample, _ => _actions.Play.Available);
        HostSampleCommand = new RelayCommand<SampleCard>(
            HostSample, card => _actions.Host.Available && card is { IsMultiplayer: true });
        OpenSampleCopyCommand = new RelayCommand<SampleCard>(OpenSampleCopy);
        OpenJoinCommand = new RelayCommand(OpenJoin, () => _actions.Play.Available);
        CloseJoinCommand = new RelayCommand(() => JoinDialog = null);
        DismissToastCommand = new RelayCommand(DismissToast);
        OpenLogsCommand = new RelayCommand(() => _ = OpenLogsAsync(), () => HasToastLog);

        Projects = new ProjectsViewModel(
            OpenDefaultNewProject,
            ExploreCommand,
            OpenProjectCommand,
            _actions.Explore.Tooltip,
            _actions.Open.Tooltip,
            [])
        {
            PlayProjectCommand = PlayProjectCommand,
            HostProjectCommand = HostProjectCommand,
            ShowInFolderCommand = ShowInFolderCommand,
            TogglePinCommand = TogglePinCommand,
            RemoveProjectCommand = RemoveProjectCommand,
            LocateProjectCommand = LocateProjectCommand,
            BrowseSamplesCommand = new RelayCommand(() => ActiveNav = "samples"),
        };
        Projects.UpdateTooltips(_actions.Explore.Tooltip, _actions.Open.Tooltip, _actions.Play.Tooltip);
        Projects.UpdateHostTooltip(_actions.Host.Tooltip);
        Refresh();
        Templates = new TemplatesViewModel(version, loadThumbnail, OpenTemplateCommand);
        Samples = new SamplesViewModel(
            PlaySampleCommand, OpenSampleCopyCommand, _actions.Play.Tooltip, loadThumbnail,
            HostSampleCommand, _actions.Host.Tooltip);
        ReloadCatalog(_engine);

        Learn = new LearnViewModel(
            url => _ = OpenUrlAsync(url),
            () => ActiveNav = "samples",
            ExploreCommand,
            _actions.Explore.Tooltip);

        NavItems =
        [
            new() { Key = "projects",  Label = "Projects",  Icon = "IconFolder", Count = ProjectCount() },
            new() { Key = "templates", Label = "Templates", Icon = "IconLayers", Count = TemplateCount() },
            new() { Key = "samples",   Label = "Samples",   Icon = "IconSparkle", Count = SampleCount() },
            new() { Key = "learn",     Label = "Learn",     Icon = "IconBook" },
        ];
        Projects.Projects.CollectionChanged += (_, _) =>
        {
            NavItems[0].Count = ProjectCount();
            OnPropertyChanged(nameof(ViewTitle));
            OnPropertyChanged(nameof(ShowViewToggle));
        };

        Projects.IsGridView = settings.Current.ProjectsView != LauncherSettings.ListView;
        Projects.ViewChosen = grid => ApplyUpdate(_settings.SetProjectsView(grid));
        Hosting = new HostingViewModel(
            () => hosting.NewSession(new EngineLauncher(_runner, _engine)),
            hosting.Network,
            () => _settings.Current.HostPort,
            _post,
            ShowNotice,
            Ask,
            () => ActiveNav = "settings");
        Hosting.PropertyChanged += (_, args) =>
        {
            if (args.PropertyName == nameof(HostingViewModel.IsPanelOpen))
            {
                OnPropertyChanged(nameof(IsBackgroundEnabled));
            }
            else if (args.PropertyName == nameof(HostingViewModel.IsHosting) && _quitPrompt is not null
                && !Hosting.IsBusy)
            {
                _quitPrompt = null;
                Prompt = null;
            }
        };
        Settings = new SettingsViewModel(settings, ApplyUpdate, message => ApplyNotice(EarlyExitNotice.Failure(message)),
            VersionLabel.Format(version));

        CurrentTab = Projects;
        UpdateActiveFlags();
    }

    private static SettingsEditor NewSettings()
    {
        var fileSystem = new PhysicalFileSystem();
        var userData = new UserDataDirectory();
        var store = new LauncherSettingsStore(fileSystem, SharedLog, userData);
        return new SettingsEditor(store, new EngineLocator(fileSystem));
    }

    private static RecentProjectsStore NewRecents()
    {
        var userData = new UserDataDirectory();
        return new RecentProjectsStore(
            new PhysicalFileSystem(), SharedLog, userData, TimeProvider.System, HostOsDetector.Current());
    }

    // A missing or unreadable image is no thumbnail; the card shows its placeholder.
    private static Bitmap? LoadThumbnail(string path)
    {
        try
        {
            return File.Exists(path) ? new Bitmap(path) : null;
        }
        catch (Exception exception) when (
            exception is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException
                or InvalidOperationException)
        {
            return null;
        }
    }

    private LaunchCoordinator NewCoordinator(EngineLocation engine) =>
        new(new EngineLauncher(_runner, engine), _watcher, ShowNotice, _editors);

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
        Projects.UpdateTooltips(_actions.Explore.Tooltip, _actions.Open.Tooltip, _actions.Play.Tooltip);
        Projects.UpdateHostTooltip(_actions.Host.Tooltip);
        HostProjectCommand.NotifyCanExecuteChanged();
        Learn.UpdateTooltip(_actions.Explore.Tooltip);
        ExploreCommand.NotifyCanExecuteChanged();
        OpenProjectCommand.NotifyCanExecuteChanged();
        PlayProjectCommand.NotifyCanExecuteChanged();
        Samples.UpdatePlay(_actions.Play.Tooltip);
        Samples.UpdateHost(_actions.Host.Tooltip);
        ReloadCatalog(engine);
        TemplatesNavItem.Count = TemplateCount();
        SamplesNavItem.Count = SampleCount();
        PlaySampleCommand.NotifyCanExecuteChanged();
        HostSampleCommand.NotifyCanExecuteChanged();
        OpenJoinCommand.NotifyCanExecuteChanged();
        OnPropertyChanged(nameof(JoinTooltip));
        OnPropertyChanged(nameof(EngineBannerText));
        OnPropertyChanged(nameof(HasEngineBanner));
    }

    // One read of the engine's templates folder feeds both tabs.
    private void ReloadCatalog(EngineLocation engine)
    {
        var contents = _catalog.Load(engine.Folder);
        _templates = contents.Templates;
        _samples = contents.Samples;
        _templatesFolder = contents.TemplatesFolder;
        Templates.Reload(contents);
        Samples.Reload(contents);
    }

    // ----- Derived view state: the header table, per tab -----
    public string ViewTitle => Header.Title;
    public string ViewSubtitle => Header.Subtitle;
    public string SearchPlaceholder => Header.SearchPlaceholder;
    public bool ShowSearch => Header.ShowSearch;
    public bool ShowViewToggle => Header.ShowViewToggle;

    // Open existing... belongs to the Projects tab alone.
    public bool ShowOpenExisting => LauncherTabs.Parse(ActiveNav) == LauncherTab.Projects;

    private TabHeader Header => LauncherTabs.Header(LauncherTabs.Parse(ActiveNav), Projects.IsEmpty);

    private string ProjectCount() => Projects.Projects.Count.ToString();

    private NavItemViewModel TemplatesNavItem => NavItems.Single(item => item.Key == "templates");

    private string TemplateCount() => Templates.Templates.Count.ToString();

    private NavItemViewModel SamplesNavItem => NavItems.Single(item => item.Key == "samples");

    private string SampleCount() => Samples.Samples.Count.ToString();

    // The task is dropped on purpose: failures and early exits arrive through ShowNotice.
    private void Explore()
    {
        if (IsInteractive)
        {
            _ = _coordinator.ExploreEngineAsync(_settings.Current.ToLaunchOptions());
        }
    }

    // Reads the stored list again, as the editor may have recorded a project meanwhile.
    // A read that failed keeps the list on screen as it is.
    public void Refresh()
    {
        if (!_recents.Load())
        {
            return;
        }

        ShowStored();
        CheckMissingInBackground();
    }

    // The flags are checked off the UI thread and applied when done; every other redraw reuses the last result.
    private void CheckMissingInBackground()
    {
        var entries = _recents.Entries();
        if (entries.Count == 0)
        {
            return;
        }

        var keys = entries.Select(entry => _recents.ProjectKey(entry.Path)).ToList();
        _runInBackground(() =>
        {
            var missing = new Dictionary<string, bool>();
            for (var index = 0; index < entries.Count; index++)
            {
                missing[keys[index]] = RecentProjectsView.IsMissing(_fileSystem, entries[index]);
            }

            _post(() =>
            {
                _missing = missing;
                ShowStored();
            });
        });
    }

    // Leaves the list alone when nothing it shows has changed, which spares re-decoding the thumbnails.
    // The missing and in-editor flags count as shown: they change while the stored entries stay the same.
    private void ShowStored()
    {
        var shown = RecentProjectsView.Sort(_recents.Entries())
            .Select(entry => (
                entry,
                RecentProjectsView.EditedText(entry.LastOpenedUtc, _now()),
                _missing.GetValueOrDefault(_recents.ProjectKey(entry.Path)),
                _editors.IsRunning(entry.Path)))
            .ToList();
        if (shown.SequenceEqual(_shown))
        {
            return;
        }

        _shown = shown;
        Projects.SetProjects(shown.Select(ToProject).ToList());
    }

    private Project ToProject((RecentProject Entry, string Edited, bool Missing, bool InEditor) item)
    {
        var entry = item.Entry;
        var legacy = RecentProjectPaths.IsLegacyFile(entry.Path);
        return new Project
        {
            Name = entry.Name,
            Path = entry.Path,
            Edited = item.Edited,
            Pinned = entry.Pinned,
            IsMissing = item.Missing,
            IsInEditor = item.InEditor,
            Thumbnail = legacy || item.Missing
                ? null
                : _loadThumbnail(System.IO.Path.Combine(entry.Path, "thumbnail.png")),
            GlowColor = GlowFor(entry.Name),
        };
    }

    private static bool IsPresent(Project? project) => project is not { IsMissing: true };

    private static readonly Color[] Glows =
    [
        Color.Parse("#8C1FB8D4"), Color.Parse("#85A78BFA"), Color.Parse("#7A4ADE80"), Color.Parse("#80F59E0B"),
    ];

    // Stable per name, so a card keeps its color from one launch to the next.
    private static Color GlowFor(string name) => Glows[name.Sum(letter => (int)letter) % Glows.Length];

    // Another process may have changed the file, so it is read again before each change that is kept.
    // A file that cannot be read is left alone, as saving would replace what is in it.
    private void ChangeStored(Func<bool> change)
    {
        if (!_recents.Load())
        {
            ApplyNotice(EarlyExitNotice.Failure($"Could not read {_recents.FilePath}"));
            return;
        }

        if (change() && !_recents.Save())
        {
            ApplyNotice(EarlyExitNotice.Failure($"Could not save the recent projects list to {_recents.FilePath}"));
        }

        ShowStored();
    }

    private void TogglePin(Project? project)
    {
        if (IsInteractive && project is { IsRealCard: true })
        {
            ChangeStored(() => _recents.TogglePinned(project.Path));
        }
    }

    private void RemoveProject(Project? project)
    {
        if (IsInteractive && project is { IsRealCard: true })
        {
            ChangeStored(() => _recents.Remove(project.Path));
        }
    }

    private void HostProject(Project? project)
    {
        if (IsInteractive && project is { IsRealCard: true, IsMissing: false } && _actions.Host.Available)
        {
            Hosting.Host(project.Path, project.Name);
        }
    }

    // Escape: cancels the question, else closes the Join dialog, else hides the hosting panel.
    private void CloseOverlay()
    {
        if (!IsInteractive)
        {
            return;
        }

        if (Prompt is { } prompt)
        {
            if (ReferenceEquals(prompt, _quitPrompt))
            {
                _quitPrompt = null;
                Prompt = null;
            }
            else
            {
                prompt.CancelCommand.Execute(null);
            }
        }
        else if (JoinDialog is not null)
        {
            JoinDialog = null;
        }
        else
        {
            Hosting.HidePanel();
        }
    }

    // True when the window may close now. While a project is being created the copy is cancelled first and the window
    // closes once its hidden folder is gone. While a game is hosted or starting it asks first and returns false:
    // Stop server and quit closes for real, Keep hosting minimizes, and Escape leaves the window as it is.
    // When the system is shutting down there is no one to ask, so the server is stopped and the close allowed.
    public bool RequestClose() => RequestClose(false);

    public bool RequestClose(bool systemShutdown)
    {
        if (!_quitApproved && NewProject is { IsCreating: true } creating)
        {
            // A system shutdown cannot wait, and a second request means the user wants out: both close anyway and
            // leave the hidden folder to the next create of that name.
            if (systemShutdown)
            {
                creating.CancelCreate(() => { });
            }
            else if (!creating.IsCancelling)
            {
                creating.CancelCreate(() =>
                {
                    NewProject = null;
                    CloseWindow?.Invoke();
                });
                return false;
            }
        }

        if (_quitApproved || !Hosting.IsBusy)
        {
            return true;
        }

        if (systemShutdown)
        {
            StopHosting();
            return true;
        }

        if (_quitPrompt is null || !ReferenceEquals(Prompt, _quitPrompt))
        {
            JoinDialog = null;
            Ask(
                new PromptRequest(
                    "Stop hosting?",
                    "A game is being hosted. Quitting stops the server and ends it for everyone in it.",
                    "Stop server and quit",
                    "Keep hosting"),
                QuitAnswered);
            _quitPrompt = Prompt;
        }

        return false;
    }

    // Stops a hosted server synchronously; the last thing the launcher does before it exits.
    public void StopHosting()
    {
        try
        {
            Hosting.Stop();
        }
        catch (Exception exception) when (exception is not OperationCanceledException)
        {
            // The launcher is leaving; a server that cannot be stopped has nothing more to report.
        }
    }

    private void QuitAnswered(bool stopAndQuit)
    {
        _quitPrompt = null;
        if (!stopAndQuit)
        {
            if (Hosting.IsBusy)
            {
                MinimizeWindow?.Invoke();
            }

            return;
        }

        StopHosting();
        _quitApproved = true;
        CloseWindow?.Invoke();
    }

    // One question at a time; asking again replaces the one on screen.
    private void Ask(PromptRequest request, Action<bool> answered) =>
        Prompt = new ConfirmPromptViewModel(request, confirmed =>
        {
            Prompt = null;
            answered(confirmed);
        });

    private void PlayProject(Project? project)
    {
        if (IsInteractive && project is { IsRealCard: true } && _actions.Play.Available)
        {
            _ = _coordinator.PlayProjectAsync(project.Path);
        }
    }

    // A legacy project is a file, so its folder is the one that holds it.
    private async Task ShowInFolderAsync(Project? project)
    {
        if (!IsInteractive || project is not { IsRealCard: true })
        {
            return;
        }

        var folder = RecentProjectPaths.IsLegacyFile(project.Path) ? ContainingFolder(project.Path) : project.Path;
        var shown = false;
        try
        {
            shown = ShowFolder is not null && await ShowFolder(folder);
        }
        catch (Exception exception) when (exception is not OperationCanceledException)
        {
            shown = false;
        }

        if (!shown)
        {
            ApplyNotice(EarlyExitNotice.Failure($"Could not open the folder {folder}"));
        }
    }

    private static string ContainingFolder(string path)
    {
        var cut = path.LastIndexOfAny(['/', '\\']);
        if (cut < 0)
        {
            return ".";
        }

        // A root keeps its separator: "/" and "C:/".
        var folder = path[..cut];
        return folder.Length == 0 || folder.EndsWith(':') ? path[..(cut + 1)] : folder;
    }

    private void OpenProject(Project? project)
    {
        if (IsInteractive && project is { IsRealCard: true, IsMissing: false } && _actions.Open.Available)
        {
            OpenRecorded(project.Path);
        }
    }

    private void OpenRecorded(string path, string? template = null)
    {
        RecordOpened(path, template);
        _ = _coordinator.OpenProjectAsync(path, _settings.Current.ToLaunchOptions());
    }

    private void RecordOpened(string path, string? template) =>
        ChangeStored(() =>
        {
            _recents.RecordOpened(path, template);
            return true;
        });

    // The header button and the first-run card start from Empty 3D, or the first usable template without it.
    private void OpenDefaultNewProject()
    {
        var usable = Templates.Templates.Where(card => card.IsAvailable).ToList();
        var card = usable.FirstOrDefault(item => item.Id == DefaultTemplateId) ?? usable.FirstOrDefault();
        if (card is null)
        {
            ApplyNotice(EarlyExitNotice.Failure(
                Templates.IsEmpty
                    ? $"No project templates were found in {_templatesFolder}. Check the engine's templates folder."
                    : "Every template needs a newer engine than this launcher belongs to."));
            return;
        }

        OpenTemplate(card);
    }

    private void OpenTemplate(TemplateCard? card)
    {
        var entry = card is { IsAvailable: true }
            ? _templates.FirstOrDefault(item => item.Manifest.Id == card.Id)
            : null;
        if (entry is null)
        {
            return;
        }

        ShowNewProject(entry);
    }

    private void OpenSampleCopy(SampleCard? card)
    {
        var entry = card is null ? null : _samples.FirstOrDefault(item => item.Manifest.Id == card.Id);
        if (entry is not null)
        {
            ShowNewProject(entry);
        }
    }

    private void ShowNewProject(TemplateEntry entry)
    {
        if (NewProject is not null)
        {
            return;
        }

        NewProject = new NewProjectViewModel(
            entry,
            _creator,
            _settings.Current.ProjectsDirectory,
            () => PickAsync(PickProjectsFolder),
            new DialogScheduling(_runInBackground, _post, NewProjectCheckDelay, RunCheckInBackground),
            (folder, openInEditor) => FinishCreate(entry, folder, openInEditor),
            () => NewProject = null);
    }

    private Task<TargetCheck> RunCheckInBackground(Func<TargetCheck> check)
    {
        var done = new TaskCompletionSource<TargetCheck>();
        _runInBackground(() =>
        {
            try
            {
                done.SetResult(check());
            }
            catch (Exception exception)
            {
                done.SetException(exception);
            }
        });
        return done.Task;
    }

    private void FinishCreate(TemplateEntry template, string folder, bool openInEditor)
    {
        NewProject = null;
        if (openInEditor && _actions.Open.Available)
        {
            OpenRecorded(folder, template.Manifest.Id);
            return;
        }

        RecordOpened(folder, template.Manifest.Id);
        if (openInEditor)
        {
            ApplyNotice(EarlyExitNotice.Failure(
                $"Created {folder}, but it was not opened. {_actions.Open.Tooltip ?? "The editor could not be found."}"));
        }
    }

    // Points the card at the folder the project moved to; a choice that is not a project changes nothing.
    private async Task LocateAsync(Project? project)
    {
        if (!IsInteractive || project is not { IsRealCard: true } || await PickAsync(PickProjectFolder) is not { } chosen)
        {
            return;
        }

        if (chosen.Length == 0)
        {
            ApplyNotice(EarlyExitNotice.Failure("Choose a folder on this computer."));
            return;
        }

        var result = ProjectPathKind.Classify(_fileSystem, chosen);
        if (result.Type == ProjectPathType.Invalid)
        {
            ApplyNotice(EarlyExitNotice.Failure(result.Error!));
            return;
        }

        // A running editor's badge stays keyed to the old path until it exits.
        ChangeStored(() => _recents.Relocate(project.Path, result.Path));
    }

    private async Task OpenExistingAsync()
    {
        if (!IsInteractive || await PickAsync(PickProjectFile) is not { } chosen)
        {
            return;
        }

        if (chosen.Length == 0)
        {
            ApplyNotice(EarlyExitNotice.Failure("Choose a file on this computer."));
            return;
        }

        var result = ProjectPathKind.Classify(_fileSystem, chosen);
        if (result.Type == ProjectPathType.Invalid)
        {
            ApplyNotice(EarlyExitNotice.Failure(result.Error!));
        }
        else if (_actions.Open.Available)
        {
            OpenRecorded(result.Path);
        }
        else
        {
            ApplyNotice(EarlyExitNotice.Failure(_actions.Open.Tooltip ?? "The editor could not be found."));
        }
    }

    // A picker that is not set, or that fails, counts as cancelled.
    private static async Task<string?> PickAsync(Func<Task<string?>>? picker)
    {
        try
        {
            return picker is null ? null : await picker();
        }
        catch (Exception exception) when (exception is not OperationCanceledException)
        {
            return null;
        }
    }

    private void PlaySample(SampleCard? card)
    {
        if (IsInteractive && card is not null && _actions.Play.Available)
        {
            _ = _coordinator.PlayProjectAsync(card.FolderPath);
        }
    }

    private void HostSample(SampleCard? card)
    {
        if (IsInteractive && card is { IsMultiplayer: true } && _actions.Host.Available)
        {
            Hosting.Host(card.FolderPath, card.Name);
        }
    }

    private void OpenJoin() =>
        JoinDialog = new JoinDialogViewModel(
            _settings.Current.LastJoin, EnginePorts.Default, JoinGame, () => JoinDialog = null);

    // The engine folder can change while the dialog is open, so the client is checked again.
    private void JoinGame(JoinTarget target)
    {
        if (!_actions.Play.Available)
        {
            ApplyNotice(EarlyExitNotice.Failure(_actions.Play.Tooltip ?? "The client could not be found."));
            return;
        }

        ApplyUpdate(_settings.SetLastJoin(target));
        Settings.RefreshLastJoin();
        JoinDialog = null;
        _ = _coordinator.JoinGameAsync(target.Address, target.Port);
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

    private async Task OpenUrlAsync(string url)
    {
        var opened = false;
        try
        {
            opened = OpenUrl is not null && await OpenUrl(url);
        }
        catch (Exception exception) when (exception is not OperationCanceledException)
        {
            opened = false;
        }

        if (!opened)
        {
            ApplyNotice(EarlyExitNotice.Failure($"Could not open {url}"));
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
