using System;
using System.IO;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Input.Platform;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using Avalonia.VisualTree;

namespace ECS3DLauncher.Shell;

// Code-behind only handles window-chrome concerns. The window is fully frameless
// (WindowDecorations="None") to match the mockup, so edge resizing is restored
// manually via BeginResizeDrag.
public class MainWindow : Window
{
    private readonly Grid _root;
    private readonly Button _maxButton;
    private readonly Border _glyphMax;
    private readonly Panel _glyphRestore;
    private bool _activatedOnce;
    private DispatcherTimer? _hostingClock;

    private const double ResizeBorder = 6;

    private static readonly Cursor s_ns = new(StandardCursorType.SizeNorthSouth);
    private static readonly Cursor s_we = new(StandardCursorType.SizeWestEast);
    private static readonly Cursor s_nwse = new(StandardCursorType.TopLeftCorner);
    private static readonly Cursor s_nesw = new(StandardCursorType.TopRightCorner);

    public MainWindow()
    {
        AvaloniaXamlLoader.Load(this);
        _root = this.FindControl<Grid>("Root") ?? throw new InvalidOperationException("Root not found");
        _maxButton = this.FindControl<Button>("MaxButton") ?? throw new InvalidOperationException("MaxButton not found");
        _glyphMax = this.FindControl<Border>("GlyphMax") ?? throw new InvalidOperationException("GlyphMax not found");
        _glyphRestore = this.FindControl<Panel>("GlyphRestore") ?? throw new InvalidOperationException("GlyphRestore not found");
        AddHandler(PointerPressedEvent, OnEdgePressed, RoutingStrategies.Tunnel);
        AddHandler(PointerMovedEvent, OnEdgeMoved, RoutingStrategies.Tunnel);
        // A press on a non-focusable element (title text, empty panels, ...) does not
        // pull keyboard focus off a TextBox, so its caret keeps blinking. Clear focus
        // whenever a press lands outside any TextBox. handledEventsToo so it still runs
        // even when a control (e.g. a Button) marks the event handled.
        AddHandler(PointerPressedEvent, OnGlobalPointerPressed, RoutingStrategies.Bubble, handledEventsToo: true);
        DataContextChanged += OnDataContextChanged;
        Activated += OnActivated;
    }

    // The dialog opens on its first field.
    private void OnJoinAddressAttached(object? sender, VisualTreeAttachmentEventArgs e) =>
        (sender as TextBox)?.Focus();

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

    private WindowEdge? EdgeAt(Point p)
    {
        if (WindowState != WindowState.Normal)
        {
            return null;
        }

        bool l = p.X <= ResizeBorder, r = p.X >= Bounds.Width - ResizeBorder;
        bool t = p.Y <= ResizeBorder, b = p.Y >= Bounds.Height - ResizeBorder;

        return CornerEdge(l, r, t, b) ?? SideEdge(l, r, t, b);
    }

    private static WindowEdge? CornerEdge(bool l, bool r, bool t, bool b)
    {
        if (t && l)
        {
            return WindowEdge.NorthWest;
        }

        if (t && r)
        {
            return WindowEdge.NorthEast;
        }

        if (b && l)
        {
            return WindowEdge.SouthWest;
        }

        if (b && r)
        {
            return WindowEdge.SouthEast;
        }

        return null;
    }

    private static WindowEdge? SideEdge(bool l, bool r, bool t, bool b)
    {
        if (t)
        {
            return WindowEdge.North;
        }

        if (b)
        {
            return WindowEdge.South;
        }

        if (l)
        {
            return WindowEdge.West;
        }

        if (r)
        {
            return WindowEdge.East;
        }

        return null;
    }

    private void OnEdgePressed(object? sender, PointerPressedEventArgs e)
    {
        if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed &&
            EdgeAt(e.GetPosition(this)) is { } edge)
        {
            BeginResizeDrag(edge, e);
            e.Handled = true;
        }
    }

    private void OnEdgeMoved(object? sender, PointerEventArgs e)
    {
        Cursor = EdgeAt(e.GetPosition(this)) switch
        {
            WindowEdge.North or WindowEdge.South => s_ns,
            WindowEdge.East or WindowEdge.West => s_we,
            WindowEdge.NorthWest or WindowEdge.SouthEast => s_nwse,
            WindowEdge.NorthEast or WindowEdge.SouthWest => s_nesw,
            _ => Cursor.Default,
        };
    }

    private void OnTitleBarPressed(object? sender, PointerPressedEventArgs e)
    {
        if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
        {
            return;
        }

        if (e.ClickCount == 2)
        {
            ToggleMaximize();
        }
        else
        {
            BeginMoveDrag(e);
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
            ToolTip.SetTip(_maxButton, isMax ? "Restore" : "Maximize");
        }
    }

    private void OnMinimize(object? sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;

    private void OnMaximize(object? sender, RoutedEventArgs e) => ToggleMaximize();

    private void OnClose(object? sender, RoutedEventArgs e) => Close();
}
