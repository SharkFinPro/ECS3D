using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;

namespace ECS3DLauncher.Shell;

// Code-behind only handles window-chrome concerns. The window is fully frameless
// (WindowDecorations="None") to match the mockup, so edge resizing is restored
// manually via BeginResizeDrag.
public partial class MainWindow : Window
{
    private const double ResizeBorder = 6;

    private static readonly Cursor s_ns = new(StandardCursorType.SizeNorthSouth);
    private static readonly Cursor s_we = new(StandardCursorType.SizeWestEast);
    private static readonly Cursor s_nwse = new(StandardCursorType.TopLeftCorner);
    private static readonly Cursor s_nesw = new(StandardCursorType.TopRightCorner);

    public MainWindow()
    {
        InitializeComponent();
        AddHandler(PointerPressedEvent, OnEdgePressed, RoutingStrategies.Tunnel);
        AddHandler(PointerMovedEvent, OnEdgeMoved, RoutingStrategies.Tunnel);
    }

    private WindowEdge? EdgeAt(Point p)
    {
        if (WindowState != WindowState.Normal)
            return null;

        bool l = p.X <= ResizeBorder, r = p.X >= Bounds.Width - ResizeBorder;
        bool t = p.Y <= ResizeBorder, b = p.Y >= Bounds.Height - ResizeBorder;

        if (t && l) return WindowEdge.NorthWest;
        if (t && r) return WindowEdge.NorthEast;
        if (b && l) return WindowEdge.SouthWest;
        if (b && r) return WindowEdge.SouthEast;
        if (t) return WindowEdge.North;
        if (b) return WindowEdge.South;
        if (l) return WindowEdge.West;
        if (r) return WindowEdge.East;
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
            return;

        if (e.ClickCount == 2)
            ToggleMaximize();
        else
            BeginMoveDrag(e);
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
            GlyphMax.IsVisible = !isMax;
            GlyphRestore.IsVisible = isMax;
            ToolTip.SetTip(MaxButton, isMax ? "Restore" : "Maximize");
        }
    }

    private void OnMinimize(object? sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;

    private void OnMaximize(object? sender, RoutedEventArgs e) => ToggleMaximize();

    private void OnClose(object? sender, RoutedEventArgs e) => Close();
}
