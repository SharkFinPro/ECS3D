using System;

namespace ECS3DLauncher.Services;

// What hosting needs from the machine: its network interfaces and a way to build a session over an engine.
public sealed record HostingServices(INetworkInfo Network, Func<EngineLauncher, HostSession> NewSession)
{
    public static HostingServices System { get; } = new(
        new SystemNetworkInfo(),
        launcher => new HostSession(launcher, new TcpPortProbe(), new UserDataDirectory()));
}
