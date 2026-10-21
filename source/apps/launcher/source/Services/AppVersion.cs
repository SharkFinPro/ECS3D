using System.Reflection;

namespace ECS3DLauncher.Services;

public static class AppVersion
{
    internal const string Fallback = "dev";

    public static string Current() => From(Assembly.GetEntryAssembly());

    public static string From(Assembly? assembly)
    {
        if (assembly is null)
        {
            return Fallback;
        }

        var informational = assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion;
        return Choose(informational, assembly.GetName().Version?.ToString());
    }

    // The informational version wins; a "+<commit hash>" build suffix is dropped.
    public static string Choose(string? informationalVersion, string? assemblyVersion)
    {
        var plus = informationalVersion?.IndexOf('+') ?? -1;
        var stripped = plus >= 0 ? informationalVersion![..plus] : informationalVersion;
        if (!string.IsNullOrWhiteSpace(stripped))
        {
            return stripped.Trim();
        }

        return string.IsNullOrWhiteSpace(assemblyVersion) ? Fallback : assemblyVersion;
    }
}
