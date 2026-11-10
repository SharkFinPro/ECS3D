namespace ECS3DLauncher.Services;

public interface ILauncherLog
{
    void Warn(string message);

    void Error(string message);
}
