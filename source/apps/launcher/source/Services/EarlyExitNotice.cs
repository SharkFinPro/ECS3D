namespace ECS3DLauncher.Services;

// Which situation a notice reports; a toast is cleared by the event that resolves its topic.
public enum NoticeTopic
{
    None,
    Hosting,
    Templates,
}

// What the toast shows. LogPath is set when there is a log to open.
public sealed record LaunchNotice(string Message, string? LogPath, NoticeTopic Topic = NoticeTopic.None);

public static class EarlyExitNotice
{
    // Only an early non-zero exit is worth a notice; every other outcome is silent.
    public static LaunchNotice? From(EarlyExitResult result)
    {
        if (result.Kind != EarlyExitKind.ExitedEarly)
        {
            return null;
        }

        var name = EngineExecutableNames.BaseName(result.Executable);
        var message = $"{name} exited with code {result.ExitCode}.";
        return new LaunchNotice(
            result.LogPath is null ? message : $"{message} Log: {result.LogPath}",
            result.LogPath);
    }

    public static LaunchNotice Failure(string error, NoticeTopic topic = NoticeTopic.None) => new(error, null, topic);
}
