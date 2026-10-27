using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ECS3DLauncher.Services;

namespace ECS3DLauncher.Tabs.Templates;

// What the disk said about a location and the target folder inside it; Target is null when it was not looked at.
public sealed record TargetCheck(string? LocationError, ProjectTargetState? Target);

// How the dialog reaches other threads. RunInBackground runs the copy and Post brings its result back to the UI thread.
// Delay is the pause before a disk check and RunCheck runs one off the UI thread.
public sealed record DialogScheduling(
    Action<Action> RunInBackground,
    Action<Action> Post,
    Func<TimeSpan, Task> Delay,
    Func<Func<TargetCheck>, Task<TargetCheck>> RunCheck);

// State of the New Project dialog: the name and location being edited, what the target folder allows, and the copy.
// Name rules apply at once; the disk checks wait for typing to pause and run off the UI thread.
public sealed class NewProjectViewModel : ObservableObject
{
    private const string CheckingText = "Checking...";
    private static readonly TimeSpan Debounce = TimeSpan.FromMilliseconds(250);

    private readonly TemplateEntry _template;
    private readonly ProjectCreator _creator;
    private readonly Func<Task<string?>> _pickLocation;
    private readonly Action<Action> _runInBackground;
    private readonly Action<Action> _post;
    private readonly Action<string, bool> _created;
    private readonly Action _close;
    private readonly Func<TimeSpan, Task> _delay;
    private readonly Func<Func<TargetCheck>, Task<TargetCheck>> _runCheck;
    private int _generation;
    private CancellationTokenSource? _createCancel;
    private Action? _cancelFinished;
    private bool _isChecking;
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

    // True from an edit until the disk check for the current name and location has answered.
    public bool IsChecking
    {
        get => _isChecking;
        private set
        {
            if (SetProperty(ref _isChecking, value))
            {
                OnPropertyChanged(nameof(TargetMessage));
                OnPropertyChanged(nameof(CanCreate));
                CreateCommand.NotifyCanExecuteChanged();
            }
        }
    }

    public bool IsCreating => IsBusy;

    public bool IsCancelling => _cancelFinished is not null;

    public bool CanCreate => !IsBusy && !IsChecking && IsTargetFree;

    public string? TargetMessage => IsChecking ? CheckingText : TargetState switch
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
    // scheduling keeps the copy and the disk checks off the UI thread and brings their results back.
    // The default name's scan of the disk stays here: it is a stat or two, and moving it would change the name
    // under the user's cursor after the dialog opens.
    public NewProjectViewModel(
        TemplateEntry template,
        ProjectCreator creator,
        string location,
        Func<Task<string?>> pickLocation,
        DialogScheduling scheduling,
        Action<string, bool> created,
        Action close)
    {
        _delay = scheduling.Delay;
        _runCheck = scheduling.RunCheck;
        _template = template;
        _creator = creator;
        _location = location;
        _pickLocation = pickLocation;
        _runInBackground = scheduling.RunInBackground;
        _post = scheduling.Post;
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
        var generation = Interlocked.Increment(ref _generation);
        NameError = ProjectNameRules.Validate(_projectName);
        if (location.Length == 0)
        {
            LocationError = "Enter a location.";
            TargetState = null;
            TargetPreview = "";
            IsChecking = false;
            NotifyCanCreate();
            return;
        }

        LocationError = null;
        TargetState = null;
        TargetPreview = CheckingText;
        IsChecking = true;
        NotifyCanCreate();
        _ = CheckAsync(generation, location, _projectName, NameError is null);
    }

    private void NotifyCanCreate()
    {
        OnPropertyChanged(nameof(CanCreate));
        CreateCommand.NotifyCanExecuteChanged();
    }

    private async Task CheckAsync(int generation, string location, string name, bool nameValid)
    {
        TargetCheck result;
        try
        {
            await _delay(Debounce).ConfigureAwait(false);
            if (Volatile.Read(ref _generation) != generation)
            {
                return;
            }

            result = await _runCheck(() => LookAtDisk(location, name, nameValid)).ConfigureAwait(false);
        }
        catch (Exception)
        {
            result = new TargetCheck(null, ProjectTargetState.Unavailable);
        }

        _post(() => ApplyCheck(generation, location, name, result));
    }

    private TargetCheck LookAtDisk(string location, string name, bool nameValid)
    {
        var problem = LocationProblem(location);
        return new TargetCheck(problem, problem is null && nameValid ? _creator.CheckTarget(location, name) : null);
    }

    private void ApplyCheck(int generation, string location, string name, TargetCheck result)
    {
        if (generation != Volatile.Read(ref _generation))
        {
            return;
        }

        LocationError = result.LocationError;
        TargetState = result.Target;
        TargetPreview = result.Target is null ? "" : Path.Combine(location, name, RecentProjectPaths.ProjectFileName);
        IsChecking = false;
        NotifyCanCreate();
    }

    private string? LocationProblem(string location) =>
        _creator.CheckLocation(location) switch
        {
            ProjectTargetState.Unavailable => "This location cannot be used.",
            ProjectTargetState.NotAFolder => "That location is a file, not a folder.",
            ProjectTargetState.Unreachable => ProjectCreator.UnreachableMessage,
            _ => null,
        };

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
        var cancel = new CancellationTokenSource();
        _createCancel = cancel;
        _runInBackground(() =>
        {
            var result = Run(name, location, cancel.Token);
            _post(() => Finish(result, openInEditor));
        });
    }

    // Stops a running copy; finished is called once its cleanup is done. Does nothing when no copy is running.
    public void CancelCreate(Action finished)
    {
        if (!IsBusy || IsCancelling)
        {
            return;
        }

        _cancelFinished = finished;
        _createCancel?.Cancel();
    }

    private ProjectCreationResult Run(string name, string location, CancellationToken cancel)
    {
        try
        {
            return _creator.Create(_template, location, name, cancel);
        }
        catch (Exception exception)
        {
            return new ProjectCreationResult(null, ProjectCreator.DescribeFailure(exception, location));
        }
    }

    private void Finish(ProjectCreationResult result, bool openInEditor)
    {
        IsBusy = false;
        _createCancel?.Dispose();
        _createCancel = null;
        if (_cancelFinished is { } finished)
        {
            _cancelFinished = null;
            _isClosed = true;
            if (result.Succeeded)
            {
                _created(result.ProjectFolder!, false);
            }

            finished();
            return;
        }

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
