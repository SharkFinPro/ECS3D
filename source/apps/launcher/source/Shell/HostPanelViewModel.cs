using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ECS3DLauncher.Services;

namespace ECS3DLauncher.Shell;

public sealed record HostPanelActions(Action OpenWindow, Action Stop, Action Hide, Action OpenSettings, Action<string> Copy);

public sealed record HostAddressRow(string Label, string Text, IRelayCommand CopyCommand);

// The hosting panel: what to share with friends and the controls for the running server.
public class HostPanelViewModel : ObservableObject
{
    private readonly Func<TimeSpan> _uptime;
    private string _runningText;

    public HostPanelViewModel(
        string name,
        int port,
        IReadOnlyList<LanAddress> addresses,
        Func<TimeSpan> uptime,
        HostPanelActions actions)
    {
        Name = name;
        Port = port;
        _uptime = uptime;
        _runningText = RunningFor(uptime());
        var portText = port.ToString(CultureInfo.InvariantCulture);
        Addresses = addresses
            .Select(item => new HostAddressRow(
                item.Label,
                $"{item.Address} : {portText}",
                new RelayCommand(() => actions.Copy($"{item.Address}:{portText}"))))
            .ToList();
        OpenWindowCommand = new RelayCommand(actions.OpenWindow);
        StopCommand = new RelayCommand(actions.Stop);
        HideCommand = new RelayCommand(actions.Hide);
        OpenSettingsCommand = new RelayCommand(actions.OpenSettings);
    }

    public string Name { get; }

    public int Port { get; }

    public string Title => $"Hosting {Name}";

    public string PortText => $"Port {Port}. Change it in Settings.";

    public string FirewallNote =>
        "The first time, your firewall may ask to allow ECS3DServer. Allow it on private networks so friends can join.";

    public IReadOnlyList<HostAddressRow> Addresses { get; }

    public bool HasAddresses => Addresses.Count > 0;

    public bool HasNoAddresses => !HasAddresses;

    public string RunningText
    {
        get => _runningText;
        private set => SetProperty(ref _runningText, value);
    }

    public IRelayCommand OpenWindowCommand { get; }

    public IRelayCommand StopCommand { get; }

    public IRelayCommand HideCommand { get; }

    public IRelayCommand OpenSettingsCommand { get; }

    public void Tick() => RunningText = RunningFor(_uptime());

    public static string RunningFor(TimeSpan uptime)
    {
        var total = (long)Math.Max(0, uptime.TotalSeconds);
        var hours = total / 3600;
        var minutes = total % 3600 / 60;
        var seconds = total % 60;
        var text = hours > 0
            ? $"{hours}h {minutes:00}m {seconds:00}s"
            : minutes > 0 ? $"{minutes}m {seconds:00}s" : $"{seconds}s";
        return $"Running for {text}";
    }
}
