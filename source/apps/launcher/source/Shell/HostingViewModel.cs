using System;
using System.Globalization;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ECS3DLauncher.Services;

namespace ECS3DLauncher.Shell;

// Shell state for hosting a game: the one HostSession, the panel, the sidebar pill and the "stop first" question.
// HostSession raises Ended on a thread-pool thread, so it is handed to the UI thread through post.
public class HostingViewModel : ObservableObject
{
    private const string LocalAddress = "127.0.0.1";

    private readonly Func<HostSession> _newSession;
    private readonly INetworkInfo _network;
    private readonly Func<int> _port;
    private readonly Action<Action> _post;
    private readonly Action<LaunchNotice> _notify;
    private readonly Action<PromptRequest, Action<bool>> _ask;
    private readonly Action _openSettings;
    private HostSession _session;
    private Task _starting = Task.CompletedTask;
    private string? _startingName;
    private HostPanelViewModel? _panel;
    private bool _isPanelOpen;

    public HostingViewModel(
        Func<HostSession> newSession,
        INetworkInfo network,
        Func<int> port,
        Action<Action> post,
        Action<LaunchNotice> notify,
        Action<PromptRequest, Action<bool>> ask,
        Action openSettings)
    {
        _newSession = newSession;
        _network = network;
        _port = port;
        _post = post;
        _notify = notify;
        _ask = ask;
        _openSettings = openSettings;
        _session = NewSession();
        ShowPanelCommand = new RelayCommand(ShowPanel);
    }

    public IRelayCommand ShowPanelCommand { get; }

    public HostPanelViewModel? Panel
    {
        get => _panel;
        private set
        {
            if (SetProperty(ref _panel, value))
            {
                OnPropertyChanged(nameof(IsHosting));
                OnPropertyChanged(nameof(PillText));
                OnPropertyChanged(nameof(PillAddress));
            }
        }
    }

    public bool IsPanelOpen
    {
        get => _isPanelOpen;
        private set => SetProperty(ref _isPanelOpen, value);
    }

    public bool IsHosting => Panel is not null;

    public string PillText => Panel is null ? "" : $"Hosting {Panel.Name}";

    public string PillAddress => Panel is null ? "" : Panel.Addresses.FirstOrDefault()?.Text ?? $"{LocalAddress} : {Panel.Port}";

    public bool IsStarting => _startingName is not null;

    public string StartingText => _startingName is null ? "" : $"Starting {_startingName}...";

    public void Tick() => Panel?.Tick();

    // Starts hosting the folder, after asking whether to stop a game that is already running or starting.
    public void Host(string folder, string name)
    {
        if (_session.IsRunning || _session.IsStarting)
        {
            _ask(
                new PromptRequest(
                    "Stop the current game?",
                    $"Stop the current game and host {name}?",
                    "Stop and host",
                    "Cancel"),
                confirmed =>
                {
                    if (confirmed)
                    {
                        _ = ReplaceAsync(folder, name);
                    }
                });
            return;
        }

        _starting = StartAsync(folder, name);
    }

    public void Stop()
    {
        _session.Stop();
        Close();
    }

    private HostSession NewSession()
    {
        var session = _newSession();
        session.Ended += OnEnded;
        return session;
    }

    private async Task ReplaceAsync(string folder, string name)
    {
        Stop();
        await _starting;
        _starting = StartAsync(folder, name);
        await _starting;
    }

    // The engine folder may have changed since the last session, so an idle one is replaced first.
    private async Task StartAsync(string folder, string name)
    {
        if (!_session.IsRunning && !_session.IsStarting)
        {
            _session.Ended -= OnEnded;
            _session = NewSession();
        }

        SetStarting(name);
        HostStartResult result;
        try
        {
            result = await _session.StartAsync(folder, name, _port(), CancellationToken.None);
        }
        catch (Exception exception) when (exception is not OperationCanceledException)
        {
            SetStarting(null);
            _notify(EarlyExitNotice.Failure(exception.Message));
            return;
        }

        SetStarting(null);
        Report(result, name);
    }

    private void Report(HostStartResult result, string name)
    {
        if (result.Kind == HostStartKind.Cancelled)
        {
            return;
        }

        if (!result.Succeeded)
        {
            _notify(new LaunchNotice(result.Message ?? "The server could not be started.", result.LogPath));
            return;
        }

        Open(name);
        if (result.Kind == HostStartKind.StartedWithoutWindow && result.Message is not null)
        {
            _notify(EarlyExitNotice.Failure(result.Message));
        }
    }

    private void Open(string name)
    {
        var port = _session.Port ?? _port();
        var session = _session;
        Panel = new HostPanelViewModel(
            name,
            port,
            LanAddresses.From(_network.GetInterfaces()),
            () => session.Uptime,
            new HostPanelActions(OpenWindow, Stop, Hide, OpenSettings, Copy));
        IsPanelOpen = true;
    }

    private void OpenWindow()
    {
        if (_session.OpenAnotherWindow() is { } error)
        {
            _notify(EarlyExitNotice.Failure(error));
        }
    }

    // Set by the window, which owns the clipboard. A copy that fails is reported on the toast.
    public Func<string, Task<bool>>? CopyToClipboard { get; set; }

    private void Copy(string text) => _ = CopyAsync(text);

    private async Task CopyAsync(string text)
    {
        var copied = false;
        try
        {
            copied = CopyToClipboard is not null && await CopyToClipboard(text);
        }
        catch (Exception exception) when (exception is not OperationCanceledException)
        {
            copied = false;
        }

        if (!copied)
        {
            _notify(EarlyExitNotice.Failure($"Could not copy {text}"));
        }
    }

    private void ShowPanel()
    {
        if (Panel is not null)
        {
            IsPanelOpen = true;
        }
    }

    private void Hide() => IsPanelOpen = false;

    private void OpenSettings()
    {
        IsPanelOpen = false;
        _openSettings();
    }

    private void Close()
    {
        IsPanelOpen = false;
        Panel = null;
    }

    private void SetStarting(string? name)
    {
        _startingName = name;
        OnPropertyChanged(nameof(IsStarting));
        OnPropertyChanged(nameof(StartingText));
    }

    private void OnEnded(HostSessionEnd end) => _post(() => Ended(end));

    private void Ended(HostSessionEnd end)
    {
        var port = (Panel?.Port ?? _port()).ToString(CultureInfo.InvariantCulture);
        var code = end.ExitCode is { } value
            ? $"exited with code {value.ToString(CultureInfo.InvariantCulture)}"
            : "exited";
        Close();
        _notify(new LaunchNotice(
            $"The hosted server {code}. If port {port} is in use, choose another port in Settings. Log: {end.LogPath}",
            end.LogPath));
    }
}
