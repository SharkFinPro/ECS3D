using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Net.Sockets;

namespace ECS3DLauncher.Services;

public sealed record LanAddress(string Label, string Address);

public static class LanAddresses
{
    // Link-local 169.254.x.x is left out: it appears when DHCP failed, and no friend can reach it.
    public static IReadOnlyList<LanAddress> From(IEnumerable<NetworkInterfaceInfo> interfaces) =>
        interfaces
            .Where(item => item.IsUp && item.Kind != NetworkInterfaceKind.Loopback)
            .SelectMany(item => item.Addresses
                .Where(IsShareable)
                .Select(address => new LanAddress(LabelOf(item), address.ToString())))
            .OrderBy(item => item.Label, StringComparer.Ordinal)
            .ThenBy(item => item.Address, StringComparer.Ordinal)
            .ToList();

    private static bool IsShareable(IPAddress address)
    {
        if (address.AddressFamily != AddressFamily.InterNetwork || IPAddress.IsLoopback(address))
        {
            return false;
        }

        var bytes = address.GetAddressBytes();
        return !(bytes[0] == 169 && bytes[1] == 254);
    }

    private static string LabelOf(NetworkInterfaceInfo item) => item.Kind switch
    {
        NetworkInterfaceKind.Wireless => "Wi-Fi",
        NetworkInterfaceKind.Ethernet => "Ethernet",
        _ => item.Name,
    };
}
