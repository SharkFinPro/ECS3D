using System;
using System.IO;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ECS3DLauncher.Services;

namespace ECS3DLauncher.Tabs.Templates;

// State of the New Project dialog: the name and location being edited, what the target folder allows, and the copy.
public sealed class NewProjectViewModel : ObservableObject
{
    private readonly TemplateEntry _template;
    private readonly ProjectCreator _creator;
    private readonly Func<Task<string?>> _pickLocation;
    private readonly Action<Action> _runInBackground;
    private readonly Action<Action> _post;
    private readonly Action<string, bool> _created;
    private readonly Action _close;
    private string _projectName;
    private string _location;
    private bool _openInEditor = true;
    private bool _isBusy;
    private bool _isClosed;
    private string? _createError;
    private string? _nameError;
    private string? _locationError;
    private ProjectTargetState? _targetState;
    private string _targetPreview = "";

    public string TemplateName => _template.Manifest.Name;

    public string TemplateDescription => _template.Manifest.Description;

    public string ProjectName
    {
        get => _projectName;
        set
        {
            if (SetProperty(ref _projectName, value))
            {
                CreateError = null;
                Evaluate();
            }
        }
    }

    public string Location
    {
        get => _location;
        set
        {
            if (SetProperty(ref _location, value))
            {
                CreateError = null;
                Evaluate();
            }
        }
    }

    public bool OpenInEditor
    {
        get => _openInEditor;
        set => SetProperty(ref _openInEditor, value);
    }

    public bool IsBusy
    {
        get => _isBusy;
        private set
        {
            if (SetProperty(ref _isBusy, value))
            {
                OnPropertyChanged(nameof(CanCreate));
                CreateCommand.NotifyCanExecuteChanged();
                CancelCommand.NotifyCanExecuteChanged();
                BrowseCommand.NotifyCanExecuteChanged();
            }
        }
    }

    // The reason the last Create failed; cleared by the next edit or attempt.
    public string? CreateError
    {
        get => _createError;
        private set
        {
            if (SetProperty(ref _createError, value))
            {
                OnPropertyChanged(nameof(HasCreateError));
            }
        }
    }

    public string? NameError
    {
        get => _nameError;
        private set
        {
            if (SetProperty(ref _nameError, value))
            {
                OnPropertyChanged(nameof(HasNameError));
            }
        }
    }

    public string? LocationError
    {
        get => _locationError;
        private set
        {
            if (SetProperty(ref _locationError, value))
            {
                OnPropertyChanged(nameof(HasLocationError));
            }
        }
    }

    // Null until both the name and the location are valid enough to look at the disk.
    public ProjectTargetState? TargetState
    {
        get => _targetState;
        private set
        {
            if (SetProperty(ref _targetState, value))
            {
                OnPropertyChanged(nameof(TargetMessage));
                OnPropertyChanged(nameof(IsTargetFree));
                OnPropertyChanged(nameof(HasTargetProblem));
            }
        }
    }

    // <location>/<name>/project.ecs3d, empty while the name or location is invalid.
    public string TargetPreview
    {
        get => _targetPreview;
        private set => SetProperty(ref _targetPreview, value);
    }

    public bool HasCreateError => CreateError is not null;

    public bool HasNameError => NameError is not null;

    public bool HasLocationError => LocationError is not null;

    public bool IsTargetFree => TargetState == ProjectTargetState.Free;

    public bool HasTargetProblem => TargetState is not null && TargetState != ProjectTargetState.Free;

    public bool CanCreate => !IsBusy && IsTargetFree;

    public string? TargetMessage => TargetState switch
    {
        ProjectTargetState.Free => "Folder name is free",
        ProjectTargetState.NotEmpty => "A folder with this name already exists and is not empty.",
        ProjectTargetState.NotAFolder => "A file with this name already exists.",
        ProjectTargetState.Unavailable => "This location cannot be used.",
        _ => null,
    };

    public IRelayCommand CreateCommand { get; }
    public IRelayCommand CancelCommand { get; }
    public IRelayCommand BrowseCommand { get; }

    // created gets the new project folder and whether to open it; the shell closes the dialog then. runInBackground
    // keeps the copy off the UI thread and post brings its result back.
    public NewProjectViewModel(
        TemplateEntry template,
        ProjectCreator creator,
        string location,
        Func<Task<string?>> pickLocation,
        Action<Action> runInBackground,
        Action<Action> post,
        Action<string, bool> created,
        Action close)
    {
        _template = template;
        _creator = creator;
        _location = location;
        _pickLocation = pickLocation;
        _runInBackground = runInBackground;
        _post = post;
        _created = created;
        _close = close;
        _projectName = ProjectNameRules.UniqueDefault(template.Manifest.Name, IsTaken);
        CreateCommand = new RelayCommand(Create, () => CanCreate);
        CancelCommand = new RelayCommand(Cancel, () => !IsBusy);
        BrowseCommand = new RelayCommand(() => _ = BrowseAsync(), () => !IsBusy);
        Evaluate();
    }

    // An unusable location is not a taken name, so a blank location keeps the plain template name.
    private bool IsTaken(string name) =>
        _creator.CheckTarget(_location.Trim(), name) is ProjectTargetState.NotEmpty or ProjectTargetState.NotAFolder;

    private void Evaluate()
    {
        var location = _location.Trim();
        NameError = ProjectNameRules.Validate(_projectName);
        LocationError = LocationProblem(location);
        var checkable = NameError is null && LocationError is null;
        TargetState = checkable ? _creator.CheckTarget(location, _projectName) : null;
        TargetPreview = checkable ? Path.Combine(location, _projectName, RecentProjectPaths.ProjectFileName) : "";
        OnPropertyChanged(nameof(CanCreate));
        CreateCommand.NotifyCanExecuteChanged();
    }

    private string? LocationProblem(string location)
    {
        if (location.Length == 0)
        {
            return "Enter a location.";
        }

        return _creator.CheckLocation(location) switch
        {
            ProjectTargetState.Unavailable => "This location cannot be used.",
            ProjectTargetState.NotAFolder => "That location is a file, not a folder.",
            _ => null,
        };
    }

    private void Cancel()
    {
        if (!IsBusy)
        {
            _isClosed = true;
            _close();
        }
    }

    private void Create()
    {
        if (!CanCreate)
        {
            return;
        }

        var name = _projectName;
        var location = _location.Trim();
        var openInEditor = OpenInEditor;
        CreateError = null;
        IsBusy = true;
        _runInBackground(() =>
        {
            var result = Run(name, location);
            _post(() => Finish(result, openInEditor));
        });
    }

    private ProjectCreationResult Run(string name, string location)
    {
        try
        {
            return _creator.Create(_template, location, name);
        }
        catch (Exception exception)
        {
            return new ProjectCreationResult(null, $"Could not create the project: {exception.Message}");
        }
    }

    private void Finish(ProjectCreationResult result, bool openInEditor)
    {
        IsBusy = false;
        if (result.Succeeded)
        {
            _isClosed = true;
            _created(result.ProjectFolder!, openInEditor);
            return;
        }

        CreateError = result.Error;
        Evaluate();
    }

    private async Task BrowseAsync()
    {
        if (IsBusy)
        {
            return;
        }

        var chosen = await _pickLocation();
        if (chosen is null || _isClosed)
        {
            return;
        }

        if (chosen.Length == 0)
        {
            CreateError = "Choose a folder on this computer.";
            return;
        }

        Location = chosen;
    }
}
