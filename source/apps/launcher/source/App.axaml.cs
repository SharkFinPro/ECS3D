using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using System;
using System.Threading.Tasks;
using Avalonia.Markup.Xaml;
using Avalonia.Threading;
using ECS3DLauncher.Services;
using ECS3DLauncher.Shell;

namespace ECS3DLauncher;

public class App : Application
{
    private readonly UnhandledErrorPolicy _errors =
        new(MainWindowViewModel.SharedLog, new UserDataDirectory().LauncherLogFile());

    public override void Initialize()
    {
        AvaloniaXamlLoader.Load(this);
        WatchUnhandledErrors();
#if DEBUG
        // Bridges to the standalone AvaloniaUI Developer Tools app; press F12
        // at runtime to connect the live inspector. Debug builds only.
        this.AttachDeveloperTools();
#endif
    }

    private void WatchUnhandledErrors()
    {
        Dispatcher.UIThread.UnhandledException += (_, e) => e.Handled = _errors.HandleUiException(e.Exception);
        AppDomain.CurrentDomain.UnhandledException +=
            (_, e) => _errors.LogBackgroundException("AppDomain", e.ExceptionObject);
        TaskScheduler.UnobservedTaskException +=
            (_, e) => _errors.LogBackgroundException("unobserved task", e.Exception);
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            // Wire the view to its view model explicitly - no ViewLocator needed
            // while there is a single window.
            var viewModel = new MainWindowViewModel();
            desktop.MainWindow = new MainWindow
            {
                DataContext = viewModel
            };
            _errors.ShowNotice = viewModel.ShowNotice;
            desktop.Exit += (_, _) => viewModel.StopHosting();
        }

        base.OnFrameworkInitializationCompleted();
    }
}
