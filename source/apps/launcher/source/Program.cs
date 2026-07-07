using System;
using Avalonia;

namespace ECS3DLauncher;

internal static class Program
{
    // Avalonia entry point. Keep this minimal — no app logic lives here.
    [STAThread]
    public static void Main(string[] args) => BuildAvaloniaApp()
        .StartWithClassicDesktopLifetime(args);

    public static AppBuilder BuildAvaloniaApp() => AppBuilder.Configure<App>()
        .UsePlatformDetect()
        .WithInterFont()
        .LogToTrace();
}
