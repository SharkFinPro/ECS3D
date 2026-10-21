namespace ECS3DLauncher.Services;

public static class VersionLabel
{
    // "1.2.3" shows as "v1.2.3"; the "dev" fallback and an already prefixed version are left alone.
    public static string Format(string version)
    {
        if (version == AppVersion.Fallback || version.StartsWith('v'))
        {
            return version;
        }

        return "v" + version;
    }
}
