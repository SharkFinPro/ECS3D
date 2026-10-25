using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Net.NetworkInformation;

namespace ECS3DLauncher.Services;

public sealed class SystemNetworkInfo : INetworkInfo
{
    public IReadOnlyList<NetworkInterfaceInfo> GetInterfaces()
    {
        var result = new List<NetworkInterfaceInfo>();
        foreach (var item in NetworkInterface.GetAllNetworkInterfaces())
        {
            try
            {
                var properties = item.GetIPProperties();
                var addresses = properties.UnicastAddresses.Select(address => address.Address).ToList();
                var kind = KindOf(item.NetworkInterfaceType);
                var hasGateway = properties.GatewayAddresses.Any(gateway => !gateway.Address.Equals(IPAddress.Any));
                result.Add(new NetworkInterfaceInfo(item.Name, kind, IsUsable(item), hasGateway, addresses));
            }
            catch (NetworkInformationException)
            {
                // An interface that cannot report its addresses cannot be shared, so it is left out.
            }
        }

        return result;
    }

    // Linux VPN interfaces (tun, wireguard) report Unknown while carrying traffic.
    private static bool IsUsable(NetworkInterface item) =>
        item.OperationalStatus is OperationalStatus.Up or OperationalStatus.Unknown;

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
