namespace ECS3DLauncher.Services;

public sealed record LaunchResult(IRunningProcess? Process, string? Error)
{
    public bool Succeeded => Process is not null;

    public static LaunchResult Started(IRunningProcess process) => new(process, null);

    public static LaunchResult Failed(string error) => new(null, error);
}
