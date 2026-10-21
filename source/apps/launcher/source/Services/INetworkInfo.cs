using System.Collections.Generic;

namespace ECS3DLauncher.Services;

public interface INetworkInfo
{
    IReadOnlyList<NetworkInterfaceInfo> GetInterfaces();
}
