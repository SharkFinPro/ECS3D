using System.Collections.Generic;
using System.Net;

namespace ECS3DLauncher.Services;

public enum NetworkInterfaceKind
{
    Wireless,
    Ethernet,
    Loopback,
    Other,
}

public sealed record NetworkInterfaceInfo(
    string Name,
    NetworkInterfaceKind Kind,
    bool IsUp,
    IReadOnlyList<IPAddress> Addresses);
