using System.Globalization;

namespace ECS3DLauncher.Services;

public static class SettingsInput
{
    public const int MinPort = 1;
    public const int MaxPort = 65535;

    // A whole number from 1 to 65535 once trimmed; no sign, no separators.
    public static bool TryParsePort(string? text, out int port)
    {
        port = 0;
        if (text is null)
        {
            return false;
        }

        if (!int.TryParse(text.Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out var value))
        {
            return false;
        }

        if (!IsPort(value))
        {
            return false;
        }

        port = value;
        return true;
    }

    public static bool IsPort(int value) => value >= MinPort && value <= MaxPort;
}
