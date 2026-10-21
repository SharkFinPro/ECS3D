using System.Threading;
using System.Threading.Tasks;

namespace ECS3DLauncher.Services;

public interface IPortProbe
{
    // True when a TCP connection to host:port is accepted. Never throws for an unreachable port.
    Task<bool> AcceptsConnectionAsync(string host, int port, CancellationToken cancellationToken);
}
