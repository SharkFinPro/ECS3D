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
    // Two interfaces with the same label get their interface name added so the rows stay apart.
    // Interfaces with a default gateway come first: host-only, Hyper-V and WSL adapters have none and a friend cannot
    // reach them, so the first row (also the sidebar pill) is the address that can be shared.
    public static IReadOnlyList<LanAddress> From(IEnumerable<NetworkInterfaceInfo> interfaces)
    {
        var usable = interfaces
            .Where(item => item.IsUp && item.Kind != NetworkInterfaceKind.Loopback)
            .Select(item => (Item: item, Addresses: item.Addresses.Where(IsShareable).ToList()))
            .Where(entry => entry.Addresses.Count > 0)
            .ToList();
        var shared = usable
            .GroupBy(entry => LabelOf(entry.Item))
            .Where(group => group.Count() > 1)
            .Select(group => group.Key)
            .ToHashSet();

        return usable
            .SelectMany(entry => entry.Addresses.Select(address =>
                (entry.Item.HasGateway, Row: new LanAddress(FullLabel(entry.Item, shared), address.ToString()))))
            .OrderByDescending(entry => entry.HasGateway)
            .ThenBy(entry => entry.Row.Label, StringComparer.Ordinal)
            .ThenBy(entry => entry.Row.Address, StringComparer.Ordinal)
            .Select(entry => entry.Row)
            .ToList();
    }

    private static bool IsShareable(IPAddress address)
    {
        if (address.AddressFamily != AddressFamily.InterNetwork || IPAddress.IsLoopback(address))
        {
            return false;
        }

        var bytes = address.GetAddressBytes();
        return !(bytes[0] == 169 && bytes[1] == 254);
    }

    private static string FullLabel(NetworkInterfaceInfo item, HashSet<string> shared)
    {
        var label = LabelOf(item);
        return shared.Contains(label) ? $"{label} ({item.Name})" : label;
    }

    private static string LabelOf(NetworkInterfaceInfo item) => item.Kind switch
    {
        NetworkInterfaceKind.Wireless => "Wi-Fi",
        NetworkInterfaceKind.Ethernet => "Ethernet",
        _ => item.Name,
    };
}
