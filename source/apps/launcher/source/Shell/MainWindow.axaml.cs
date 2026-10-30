using System;
using System.IO;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Input.Platform;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using Avalonia.VisualTree;

namespace ECS3DLauncher.Shell;

// Code-behind only handles window-chrome concerns.
public class MainWindow : Window
{
    private readonly Grid _root;
    private readonly Button _maxButton;
    private readonly Border _glyphMax;
    private readonly Panel _glyphRestore;
    private bool _activatedOnce;
    private DispatcherTimer? _hostingClock;

    public MainWindow()
    {
        AvaloniaXamlLoader.Load(this);
        _root = this.FindControl<Grid>("Root") ?? throw new InvalidOperationException("Root not found");
        _maxButton = this.FindControl<Button>("MaxButton") ?? throw new InvalidOperationException("MaxButton not found");
        _glyphMax = this.FindControl<Border>("GlyphMax") ?? throw new InvalidOperationException("GlyphMax not found");
        _glyphRestore = this.FindControl<Panel>("GlyphRestore") ?? throw new InvalidOperationException("GlyphRestore not found");
        // A press on a non-focusable element (title text, empty panels, ...) does not
        // pull keyboard focus off a TextBox, so its caret keeps blinking. Clear focus
        // whenever a press lands outside any TextBox. handledEventsToo so it still runs
        // even when a control (e.g. a Button) marks the event handled.
        AddHandler(PointerPressedEvent, OnGlobalPointerPressed, RoutingStrategies.Bubble, handledEventsToo: true);
        DataContextChanged += OnDataContextChanged;
        Activated += OnActivated;
    }

    // Opening a file with the OS default app is a view concern; the view model only decides when.
    private void OnDataContextChanged(object? sender, EventArgs e)
    {
        if (DataContext is MainWindowViewModel viewModel)
        {
            viewModel.OpenLogFile = OpenWithDefaultAppAsync;
            viewModel.ShowFolder = ShowFolderAsync;
            viewModel.OpenUrl = OpenInBrowserAsync;
            viewModel.PickProjectFolder = () => PickFolderAsync("Locate the project folder");
            viewModel.PickProjectFile = PickProjectFileAsync;
            viewModel.Hosting.CopyToClipboard = CopyToClipboardAsync;
            viewModel.CloseWindow = Close;
            viewModel.MinimizeWindow = () => WindowState = WindowState.Minimized;
            StartHostingClock(viewModel);
            viewModel.PickProjectsFolder = () => PickFolderAsync("Choose where to create the project");
        }
    }

    // The hosting panel's running time is re-read once a second.
    private void StartHostingClock(MainWindowViewModel viewModel)
    {
        _hostingClock?.Stop();
        _hostingClock = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        _hostingClock.Tick += (_, _) => viewModel.Hosting.Tick();
        _hostingClock.Start();
    }

    private async Task<bool> CopyToClipboardAsync(string text)
    {
        if (Clipboard is not { } clipboard)
        {
            return false;
        }

        await clipboard.SetTextAsync(text);
        return true;
    }

    private async Task<bool> OpenWithDefaultAppAsync(string path)
    {
        return Launcher is { } launcher && await launcher.LaunchFileInfoAsync(new FileInfo(path));
    }

    private async Task<bool> OpenInBrowserAsync(string url)
    {
        return Launcher is { } launcher && await launcher.LaunchUriAsync(new Uri(url));
    }

    private async Task<bool> ShowFolderAsync(string path)
    {
        return Launcher is { } launcher && await launcher.LaunchDirectoryInfoAsync(new DirectoryInfo(path));
    }

    private async Task<string?> PickFolderAsync(string title)
    {
        var folders = await StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = title,
            AllowMultiple = false,
        });
        return folders.Count == 0 ? null : folders[0].TryGetLocalPath() ?? string.Empty;
    }

    // A project folder is chosen by picking the project.ecs3d inside it.
    private async Task<string?> PickProjectFileAsync()
    {
        var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Open an existing project",
            AllowMultiple = false,
            FileTypeFilter =
            [
                new FilePickerFileType("ECS3D project") { Patterns = ["*.ecs3d"] },
                new FilePickerFileType("Legacy project (.json)") { Patterns = ["*.json"] },
            ],
        });
        return files.Count == 0 ? null : files[0].TryGetLocalPath() ?? string.Empty;
    }

    // The editor may have recorded a project while the launcher was in the background.
    private void OnActivated(object? sender, EventArgs e)
    {
        // The first activation is the launch itself, and the list was just loaded.
        if (_activatedOnce)
        {
            (DataContext as MainWindowViewModel)?.Refresh();
        }

        _activatedOnce = true;
    }

    private void OnGlobalPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        // Ignore presses inside a TextBox - those should keep (or take) focus.
        if (e.Source is Visual v && v.FindAncestorOfType<TextBox>(includeSelf: true) is not null)
        {
            return;
        }

        if (FocusManager?.GetFocusedElement() is TextBox)
        {
            _root.Focus();
        }
    }

    private void ToggleMaximize() =>
        WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;

    // Swap the maximize glyph for a restore glyph while maximized.
    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == WindowStateProperty)
        {
            var isMax = WindowState == WindowState.Maximized;
            _glyphMax.IsVisible = !isMax;
            _glyphRestore.IsVisible = isMax;
            var maximizeLabel = isMax ? "Restore" : "Maximize";
            ToolTip.SetTip(_maxButton, maximizeLabel);
            AutomationProperties.SetName(_maxButton, maximizeLabel);
        }
    }

    private void OnMinimize(object? sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;

    private void OnMaximize(object? sender, RoutedEventArgs e) => ToggleMaximize();

    private void OnClose(object? sender, RoutedEventArgs e) => Close();

    // Every close path (the title-bar button, Alt+F4, the OS) ends here; the view model decides.
    protected override void OnClosing(WindowClosingEventArgs e)
    {
        base.OnClosing(e);
        var shuttingDown = e.CloseReason is WindowCloseReason.OSShutdown or WindowCloseReason.ApplicationShutdown;
        if (DataContext is MainWindowViewModel viewModel && !viewModel.RequestClose(shuttingDown))
        {
            e.Cancel = true;
        }
    }
}
