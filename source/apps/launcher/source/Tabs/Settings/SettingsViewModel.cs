using System;
using System.Globalization;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ECS3DLauncher.Services;

namespace ECS3DLauncher.Tabs.Settings;

// State for the Settings tab: a thin wrapper over SettingsEditor, which decides, validates and saves.
// Every edit is applied at once; the shell hears about the outcome through the applied callback.
public sealed class SettingsViewModel : ObservableObject
{
    private readonly SettingsEditor _editor;
    private readonly Action<SettingsUpdate> _applied;
    private string _engineFolder;
    private string _projectsFolder;
    private string _portText;

    public SettingsViewModel(SettingsEditor editor, Action<SettingsUpdate> applied)
    {
        _editor = editor;
        _applied = applied;
        _engineFolder = editor.EngineFolderText;
        _projectsFolder = editor.Current.ProjectsDirectory;
        _portText = editor.Current.HostPort.ToString(CultureInfo.InvariantCulture);
        BrowseEngineCommand = new RelayCommand(() => _ = BrowseAsync(PickEngineFolder, path => EngineFolder = path));
        BrowseProjectsCommand = new RelayCommand(() => _ = BrowseAsync(PickProjectsFolder, path => ProjectsFolder = path));
        ResetEngineCommand = new RelayCommand(() => EngineFolder = "");
    }

    // Set by the view, which owns the OS folder picker.
    public Func<Task<string?>>? PickEngineFolder { get; set; }

    public Func<Task<string?>>? PickProjectsFolder { get; set; }

    public IRelayCommand BrowseEngineCommand { get; }
    public IRelayCommand BrowseProjectsCommand { get; }
    public IRelayCommand ResetEngineCommand { get; }

    public string EngineFolder
    {
        get => _engineFolder;
        set
        {
            if (!SetProperty(ref _engineFolder, value))
            {
                return;
            }

            _applied(_editor.SetEngineDirectory(value));
            OnPropertyChanged(nameof(EngineFoundText));
            OnPropertyChanged(nameof(EngineMissingText));
            OnPropertyChanged(nameof(HasEngineMissing));
            OnPropertyChanged(nameof(EngineError));
            OnPropertyChanged(nameof(HasEngineError));
        }
    }

    public string? EngineFoundText => _editor.EngineFoundText;
    public string? EngineMissingText => _editor.EngineMissingText;
    public bool HasEngineMissing => EngineMissingText is not null;
    public string? EngineError => _editor.EngineError;
    public bool HasEngineError => EngineError is not null;

    public string ProjectsFolder
    {
        get => _projectsFolder;
        set
        {
            if (SetProperty(ref _projectsFolder, value))
            {
                _applied(_editor.SetProjectsDirectory(value));
            }
        }
    }

    public bool EditorConsole
    {
        get => _editor.Current.EditorConsole;
        set => Change(_editor.SetEditorConsole(value), nameof(EditorConsole));
    }

    public bool ServerConsole
    {
        get => _editor.Current.ServerConsole;
        set => Change(_editor.SetServerConsole(value), nameof(ServerConsole));
    }

    public bool CloseAfterOpen
    {
        get => _editor.Current.CloseAfterOpen;
        set => Change(_editor.SetCloseAfterOpen(value), nameof(CloseAfterOpen));
    }

    public string PortText
    {
        get => _portText;
        set
        {
            if (!SetProperty(ref _portText, value))
            {
                return;
            }

            _applied(_editor.SetHostPort(value));
            OnPropertyChanged(nameof(PortError));
            OnPropertyChanged(nameof(HasPortError));
        }
    }

    public string? PortError => _editor.PortError;
    public bool HasPortError => PortError is not null;

    public string LastJoinText => _editor.LastJoinText;

    private void Change(SettingsUpdate update, string property)
    {
        _applied(update);
        OnPropertyChanged(property);
    }

    // A cancelled picker returns null and changes nothing.
    private static async Task BrowseAsync(Func<Task<string?>>? pick, Action<string> choose)
    {
        var path = pick is null ? null : await pick();
        if (!string.IsNullOrEmpty(path))
        {
            choose(path);
        }
    }
}
