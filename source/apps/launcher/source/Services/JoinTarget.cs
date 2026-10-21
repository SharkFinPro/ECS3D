using System.Globalization;

namespace ECS3DLauncher.Services;

public sealed record JoinTarget(string Address, int Port)
{
    private const int MinPort = 1;
    private const int MaxPort = 65535;

    public const string PortError = "The port must be a whole number from 1 to 65535.";

    public const string AddressError = "Enter the host's address.";

    // A blank port field means the default port.
    public static JoinTargetResult Parse(string? addressText, string? portText, int defaultPort)
    {
        var address = (addressText ?? string.Empty).Trim();
        if (address.Length == 0)
        {
            return JoinTargetResult.Failed(AddressError);
        }

        var port = (portText ?? string.Empty).Trim();
        if (port.Length == 0)
        {
            return JoinTargetResult.Valid(new JoinTarget(address, defaultPort));
        }

        if (!int.TryParse(port, NumberStyles.None, CultureInfo.InvariantCulture, out var number)
            || number < MinPort || number > MaxPort)
        {
            return JoinTargetResult.Failed(PortError);
        }

        return JoinTargetResult.Valid(new JoinTarget(address, number));
    }

    // Splits a pasted "address:port" into its two fields. The port comes back as text so the caller
    // can validate it. A bare IPv6 literal has several colons and is left whole; "[::1]:3000" splits.
    public static (string Address, string? Port) SplitPasted(string? text)
    {
        var trimmed = (text ?? string.Empty).Trim();
        if (trimmed.StartsWith('['))
        {
            var close = trimmed.IndexOf("]:", System.StringComparison.Ordinal);
            if (close > 1)
            {
                return (trimmed.Substring(1, close - 1), trimmed.Substring(close + 2));
            }

            return (trimmed, null);
        }

        var colon = trimmed.IndexOf(':');
        var single = colon > 0 && colon == trimmed.LastIndexOf(':') && colon < trimmed.Length - 1;
        return single ? (trimmed.Substring(0, colon), trimmed.Substring(colon + 1)) : (trimmed, null);
    }
}

public sealed record JoinTargetResult(JoinTarget? Target, string? Error)
{
    public bool IsValid => Target is not null;

    public static JoinTargetResult Valid(JoinTarget target) => new(target, null);

    public static JoinTargetResult Failed(string error) => new(null, error);
}
