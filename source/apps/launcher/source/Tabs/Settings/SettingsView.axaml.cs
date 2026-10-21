using System;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Markup.Xaml;
using Avalonia.Platform.Storage;

namespace ECS3DLauncher.Tabs.Settings;

public class SettingsView : UserControl
{
    public SettingsView()
    {
        AvaloniaXamlLoader.Load(this);
        DataContextChanged += OnDataContextChanged;
    }

    // The OS folder picker is a view concern; the view model only decides when to ask.
    private void OnDataContextChanged(object? sender, EventArgs e)
    {
        if (DataContext is SettingsViewModel viewModel)
        {
            viewModel.PickEngineFolder = () => PickFolderAsync("Choose the engine folder");
            viewModel.PickProjectsFolder = () => PickFolderAsync("Choose the projects folder");
        }
    }

    private async Task<string?> PickFolderAsync(string title)
    {
        if (TopLevel.GetTopLevel(this)?.StorageProvider is not { } storage)
        {
            return null;
        }

        var folders = await storage.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = title,
            AllowMultiple = false,
        });
        return folders.Count == 0 ? null : folders[0].TryGetLocalPath();
    }
}
