using System;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;

namespace ECS3DLauncher.Services;

public sealed class TcpPortProbe : IPortProbe
{
    private static readonly TimeSpan DefaultAttemptTimeout = TimeSpan.FromMilliseconds(500);

    private readonly TimeSpan _attemptTimeout;

    public TcpPortProbe()
        : this(DefaultAttemptTimeout)
    {
    }

    public TcpPortProbe(TimeSpan attemptTimeout)
    {
        _attemptTimeout = attemptTimeout;
    }

    // Each successful probe shows up as a rejected handshake in the server's log.
    public async Task<bool> AcceptsConnectionAsync(string host, int port, CancellationToken cancellationToken)
    {
        using var attempt = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        attempt.CancelAfter(_attemptTimeout);

        using var client = new TcpClient();
        try
        {
            await client.ConnectAsync(host, port, attempt.Token);
            return true;
        }
        catch (SocketException)
        {
            return false;
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            return false;
        }
    }
}
