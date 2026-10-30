using System;
using System.Collections.Generic;
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
    private readonly Action<NoticeTopic> _clearNotice;
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
        Action openSettings,
        Action<NoticeTopic> clearNotice)
    {
        _newSession = newSession;
        _network = network;
        _port = port;
        _post = post;
        _notify = notify;
        _ask = ask;
        _openSettings = openSettings;
        _clearNotice = clearNotice;
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

    // True while a game is hosted or a server is still coming up.
    public bool IsBusy => _session.IsRunning || _session.IsStarting || IsStarting;

    public string StartingText => _startingName is null ? "" : $"Starting {_startingName}...";

    public void Tick() => Panel?.Tick();

    // Starts hosting the folder, after asking whether to stop a game that is already running or starting.
    public void Host(string folder, string name)
    {
        if (_session.IsStarting || _startingName is not null)
        {
            return;
        }

        if (_session.IsRunning)
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
        session.Ended += end => _post(() => Ended(session, end));
        return session;
    }

    private async Task ReplaceAsync(string folder, string name)
    {
        Stop();
        await SettledAsync(_starting);
        _starting = StartAsync(folder, name);
        await _starting;
    }

    private static async Task SettledAsync(Task task)
    {
        try
        {
            await task;
        }
        catch (Exception exception) when (exception is not OperationCanceledException)
        {
            // A failed start was already reported when it happened.
        }
    }

    // The engine folder may have changed since the last session, so an idle one is replaced first.
    private async Task StartAsync(string folder, string name)
    {
        try
        {
            if (!_session.IsRunning && !_session.IsStarting)
            {
                _session = NewSession();
            }

            SetStarting(name);
            var result = await _session.StartAsync(folder, name, _port(), CancellationToken.None);
            SetStarting(null);
            Report(result, name);
        }
        catch (Exception exception) when (exception is not OperationCanceledException)
        {
            SetStarting(null);
            _notify(EarlyExitNotice.Failure(exception.Message, NoticeTopic.Hosting));
        }
    }

    private void Report(HostStartResult result, string name)
    {
        if (result.Kind == HostStartKind.Cancelled)
        {
            return;
        }

        if (!result.Succeeded)
        {
            _notify(new LaunchNotice(
                result.Message ?? "The server could not be started.", result.LogPath, NoticeTopic.Hosting));
            return;
        }

        _clearNotice(NoticeTopic.Hosting);
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
            ShareableAddresses(),
            () => session.Uptime,
            new HostPanelActions(OpenWindow, Stop, HidePanel, OpenSettings, Copy));
        IsPanelOpen = true;
    }

    // An interface list that cannot be read leaves the panel without addresses rather than without a game.
    private IReadOnlyList<LanAddress> ShareableAddresses()
    {
        try
        {
            return LanAddresses.From(_network.GetInterfaces());
        }
        catch (Exception exception) when (exception is not OperationCanceledException)
        {
            return [];
        }
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

    public void HidePanel() => IsPanelOpen = false;

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

    // A late event from a session that was stopped, replaced or never opened a panel is dropped.
    private void Ended(HostSession session, HostSessionEnd end)
    {
        if (!ReferenceEquals(session, _session) || Panel is null)
        {
            return;
        }

        var code = end.ExitCode is { } value
            ? $"exited with code {value.ToString(CultureInfo.InvariantCulture)}"
            : "exited";
        Close();
        _notify(new LaunchNotice($"The hosted server {code}. Log: {end.LogPath}", end.LogPath));
    }
}
