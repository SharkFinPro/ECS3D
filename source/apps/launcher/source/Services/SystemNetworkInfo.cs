using System.Collections.Generic;
using System.Linq;
using System.Net.NetworkInformation;

namespace ECS3DLauncher.Services;

public sealed class SystemNetworkInfo : INetworkInfo
{
    public IReadOnlyList<NetworkInterfaceInfo> GetInterfaces() =>
        NetworkInterface.GetAllNetworkInterfaces()
            .Select(item => new NetworkInterfaceInfo(
                item.Name,
                KindOf(item.NetworkInterfaceType),
                item.OperationalStatus == OperationalStatus.Up,
                item.GetIPProperties().UnicastAddresses.Select(address => address.Address).ToList()))
            .ToList();

    private static NetworkInterfaceKind KindOf(NetworkInterfaceType type) => type switch
    {
        NetworkInterfaceType.Wireless80211 => NetworkInterfaceKind.Wireless,
        NetworkInterfaceType.Ethernet or NetworkInterfaceType.GigabitEthernet
            or NetworkInterfaceType.FastEthernetT or NetworkInterfaceType.FastEthernetFx
            => NetworkInterfaceKind.Ethernet,
        NetworkInterfaceType.Loopback => NetworkInterfaceKind.Loopback,
        _ => NetworkInterfaceKind.Other,
    };
}
