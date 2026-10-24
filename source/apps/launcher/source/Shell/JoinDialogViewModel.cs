using System;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ECS3DLauncher.Services;

namespace ECS3DLauncher.Shell;

// State for the Join a game dialog: the two fields, their validation and the Join and Cancel commands.
public sealed class JoinDialogViewModel : ObservableObject
{
    private readonly int _defaultPort;
    private readonly Action<JoinTarget> _join;
    private string _address;
    private string _portText;
    private bool _edited;

    public JoinDialogViewModel(LastJoin? last, int defaultPort, Action<JoinTarget> join, Action cancel)
    {
        _defaultPort = defaultPort;
        _join = join;
        _address = last?.Address ?? "";
        _portText = (last?.Port ?? defaultPort).ToString(System.Globalization.CultureInfo.InvariantCulture);
        JoinCommand = new RelayCommand(Join, () => Target is not null);
        CancelCommand = new RelayCommand(cancel);
    }

    public IRelayCommand JoinCommand { get; }

    public IRelayCommand CancelCommand { get; }

    // A pasted "address:port" fills both fields. Only a jump of more than one character counts as a paste, so
    // typing "host:3101" by hand is left alone until Join splits it.
    public string Address
    {
        get => _address;
        set
        {
            var (address, port) = value.Length - _address.Length > 1
                ? JoinTarget.SplitPasted(value)
                : (value, null);
            if (port is not null)
            {
                SetProperty(ref _portText, port, nameof(PortText));
            }

            _address = address;
            OnPropertyChanged();
            Changed();
        }
    }

    public string PortText
    {
        get => _portText;
        set
        {
            if (SetProperty(ref _portText, value))
            {
                Changed();
            }
        }
    }

    // Shown only after the user has typed, so the empty dialog opens without a complaint.
    public string? Error => _edited ? Parsed.Error : null;

    public bool HasError => Error is not null;

    private JoinTargetResult Parsed
    {
        get
        {
            var (address, port) = JoinTarget.SplitPasted(_address);
            return JoinTarget.Parse(address, port ?? _portText, _defaultPort);
        }
    }

    private JoinTarget? Target => Parsed.Target;

    private void Changed()
    {
        _edited = true;
        OnPropertyChanged(nameof(Error));
        OnPropertyChanged(nameof(HasError));
        JoinCommand.NotifyCanExecuteChanged();
    }

    private void Join()
    {
        if (Target is { } target)
        {
            _join(target);
        }
    }
}
