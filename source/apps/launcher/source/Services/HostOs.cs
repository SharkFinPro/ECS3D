using System;

namespace ECS3DLauncher.Services;

public enum HostOs
{
    Windows,
    MacOs,
    Linux,
}

public static class HostOsDetector
{
    public static HostOs Current()
    {
        if (OperatingSystem.IsWindows())
        {
            return HostOs.Windows;
        }

        return OperatingSystem.IsMacOS() ? HostOs.MacOs : HostOs.Linux;
    }
}
