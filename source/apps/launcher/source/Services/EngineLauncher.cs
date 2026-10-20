using System.Collections.Generic;
using System.Globalization;
using System.Linq;

namespace ECS3DLauncher.Services;

public sealed class EngineLauncher
{
    private readonly IProcessRunner _runner;
    private readonly EngineLocation _engine;

    public EngineLauncher(IProcessRunner runner, EngineLocation engine)
    {
        _runner = runner;
        _engine = engine;
    }

    public LaunchResult OpenProject(string projectFolder, LaunchOptions options)
    {
        var arguments = new List<string> { "--project", projectFolder };
        AppendEditorOptions(arguments, options);
        return Launch(EngineExecutable.Editor, arguments, false);
    }

    public LaunchResult ExploreEngine(LaunchOptions options)
    {
        var arguments = new List<string>();
        AppendEditorOptions(arguments, options);
        return Launch(EngineExecutable.Editor, arguments, false);
    }

    public LaunchResult PlayProject(string projectFolder) =>
        Launch(EngineExecutable.Client, new List<string> { "--project", projectFolder }, false);

    // ECS3DServer is a console application, so it runs with its window hidden.
    public LaunchResult StartServer(string projectFolder, int port) =>
        Launch(
            EngineExecutable.Server,
            new List<string> { "--project", projectFolder, "--port", port.ToString(CultureInfo.InvariantCulture) },
            true);

    private static void AppendEditorOptions(List<string> arguments, LaunchOptions options)
    {
        if (options.EditorConsole)
        {
            arguments.Add("--console");
        }

        if (options.ServerConsole)
        {
            arguments.Add("--server-console");
        }
    }

    private LaunchResult Launch(EngineExecutable executable, IReadOnlyList<string> arguments, bool hideWindow)
    {
        if (!_engine.Found.TryGetValue(executable, out var path))
        {
            var missing = _engine.Missing.Where(item => item.Executable == executable).ToList();
            return LaunchResult.Failed(EngineLocation.Sentence(_engine.Folder, missing));
        }

        try
        {
            return LaunchResult.Started(_runner.Start(new ProcessLaunch(path, arguments, _engine.Folder, hideWindow)));
        }
        catch (ProcessStartException exception)
        {
            return LaunchResult.Failed(exception.Message);
        }
    }
}
