namespace ECS3DLauncher.Services;

public interface IProcessRunner
{
    // Throws ProcessStartException when the process cannot be started.
    IRunningProcess Start(ProcessLaunch launch);
}
