using System;
using CommunityToolkit.Mvvm.Input;

namespace ECS3DLauncher.Shell;

public sealed record PromptRequest(string Title, string Message, string ConfirmLabel, string CancelLabel);

// A modal yes/no question. The callback gets true for the confirm button and false for cancel.
public class ConfirmPromptViewModel
{
    public ConfirmPromptViewModel(PromptRequest request, Action<bool> resolved)
    {
        Title = request.Title;
        Message = request.Message;
        ConfirmLabel = request.ConfirmLabel;
        CancelLabel = request.CancelLabel;
        ConfirmCommand = new RelayCommand(() => resolved(true));
        CancelCommand = new RelayCommand(() => resolved(false));
    }

    public string Title { get; }

    public string Message { get; }

    public string ConfirmLabel { get; }

    public string CancelLabel { get; }

    public IRelayCommand ConfirmCommand { get; }

    public IRelayCommand CancelCommand { get; }
}
